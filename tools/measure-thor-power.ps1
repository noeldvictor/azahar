[CmdletBinding()]
param(
    [string]$Serial = '192.168.1.33:5555',
    [string]$Package = 'org.azahar_emu.azahar.debug',
    [ValidateRange(10, 3600)]
    [int]$DurationSeconds = 180,
    [ValidateRange(0, 600)]
    [int]$WarmupSeconds = 60,
    [ValidateRange(0.25, 10.0)]
    [double]$IntervalSeconds = 1.0,
    [ValidateRange(0.1, 100.0)]
    [double]$MaxAverageWatts = 6.0,
    [ValidateRange(0.1, 100.0)]
    [double]$MaxP95Watts = 6.0,
    [int]$ExpectedPerformanceMode = 0,
    [int]$ExpectedFanMode = 4,
    [int]$ExpectedBrightness = -1,
    [string]$ExpectedVersionName = '37053eb9d-vanilla-thor',
    [string]$ExpectedConfigSha256 =
        'EC42812B2580738DB6994126A1BB92BBEC4BBBDC11D3035330901E58ACD44E21',
    [string]$ExpectedScreenshotSha256 = '',
    [string]$OutputDirectory = '',
    [string]$AdbPath = '',
    [bool]$RequireWifiAdb = $true,
    [switch]$CaptureScreenshots,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ResolvedAdb = ''

function Get-NearestRankPercentile {
    param(
        [Parameter(Mandatory)]
        [double[]]$Values,
        [Parameter(Mandatory)]
        [ValidateRange(0.0, 1.0)]
        [double]$Fraction
    )

    if ($Values.Count -eq 0) {
        throw 'A percentile requires at least one value.'
    }

    $sorted = @($Values | Sort-Object)
    $rank = [Math]::Max(1, [Math]::Ceiling($Fraction * $sorted.Count))
    return [double]$sorted[$rank - 1]
}

function Get-SampleStatistics {
    param(
        [Parameter(Mandatory)]
        [double[]]$Values
    )

    if ($Values.Count -eq 0) {
        throw 'Statistics require at least one value.'
    }

    $measure = $Values | Measure-Object -Average -Minimum -Maximum
    return [pscustomobject]@{
        Count = $Values.Count
        Mean = [double]$measure.Average
        Median = Get-NearestRankPercentile -Values $Values -Fraction 0.50
        P95 = Get-NearestRankPercentile -Values $Values -Fraction 0.95
        Minimum = [double]$measure.Minimum
        Maximum = [double]$measure.Maximum
    }
}

function Get-ThermalSlopePerMinute {
    param(
        [Parameter(Mandatory)]
        [object[]]$Samples
    )

    if ($Samples.Count -lt 2) {
        return 0.0
    }

    $meanTime = ($Samples | Measure-Object -Property ElapsedSeconds -Average).Average
    $meanTemperature = ($Samples | Measure-Object -Property TemperatureC -Average).Average
    $covariance = 0.0
    $variance = 0.0
    foreach ($sample in $Samples) {
        $timeDelta = [double]$sample.ElapsedSeconds - $meanTime
        $temperatureDelta = [double]$sample.TemperatureC - $meanTemperature
        $covariance += $timeDelta * $temperatureDelta
        $variance += $timeDelta * $timeDelta
    }

    if ($variance -le 0.0) {
        return 0.0
    }
    return 60.0 * $covariance / $variance
}

function ConvertFrom-BatteryDump {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    function Read-BatteryBoolean([string]$Name) {
        $match = [regex]::Match(
            $Text,
            '(?mi)^\s*' + [regex]::Escape($Name) + ':\s*(true|false)\s*$'
        )
        if (-not $match.Success) {
            throw "Missing '$Name' in dumpsys battery output."
        }
        return $match.Groups[1].Value -ieq 'true'
    }

    $statusMatch = [regex]::Match($Text, '(?mi)^\s*status:\s*(\d+)\s*$')
    if (-not $statusMatch.Success) {
        throw "Missing 'status' in dumpsys battery output."
    }
    $updatesMatch = [regex]::Match($Text, '(?mi)^\s*Updates stopped:\s*(true|false)\s*$')

    return [pscustomobject]@{
        AcPowered = Read-BatteryBoolean 'AC powered'
        UsbPowered = Read-BatteryBoolean 'USB powered'
        WirelessPowered = Read-BatteryBoolean 'Wireless powered'
        Status = [int]$statusMatch.Groups[1].Value
        UpdatesStopped = $updatesMatch.Success -and $updatesMatch.Groups[1].Value -ieq 'true'
    }
}

function Test-PowerGate {
    param(
        [Parameter(Mandatory)]
        [object]$Statistics,
        [Parameter(Mandatory)]
        [double]$MaximumMean,
        [Parameter(Mandatory)]
        [double]$MaximumP95
    )

    return $Statistics.Mean -le $MaximumMean -and $Statistics.P95 -le $MaximumP95
}

function Invoke-SelfTest {
    $values = [double[]](@(1..19 | ForEach-Object { 5.0 }) + 6.0)
    $statistics = Get-SampleStatistics -Values $values
    if ($statistics.Count -ne 20 -or $statistics.Mean -ne 5.05 -or
        $statistics.Median -ne 5.0 -or $statistics.P95 -ne 5.0 -or
        $statistics.Maximum -ne 6.0) {
        throw 'Statistics self-test failed.'
    }
    if (-not (Test-PowerGate -Statistics $statistics -MaximumMean 5.1 -MaximumP95 5.1)) {
        throw 'Passing power-gate self-test failed.'
    }
    if (Test-PowerGate -Statistics $statistics -MaximumMean 5.0 -MaximumP95 5.1) {
        throw 'Failing power-gate self-test failed.'
    }

    $thermalSamples = @(
        [pscustomobject]@{ ElapsedSeconds = 0.0; TemperatureC = 30.0 },
        [pscustomobject]@{ ElapsedSeconds = 30.0; TemperatureC = 31.0 },
        [pscustomobject]@{ ElapsedSeconds = 60.0; TemperatureC = 32.0 }
    )
    $slope = Get-ThermalSlopePerMinute -Samples $thermalSamples
    if ([Math]::Abs($slope - 2.0) -gt 0.000001) {
        throw 'Thermal-slope self-test failed.'
    }

    $realDump = @'
Current Battery Service state:
  AC powered: false
  USB powered: false
  Wireless powered: false
  status: 3
'@
    $realState = ConvertFrom-BatteryDump -Text $realDump
    if ($realState.AcPowered -or $realState.UsbPowered -or $realState.WirelessPowered -or
        $realState.Status -ne 3 -or $realState.UpdatesStopped) {
        throw 'Real-battery parser self-test failed.'
    }

    $fakeDump = "Updates stopped: true`n$realDump"
    if (-not (ConvertFrom-BatteryDump -Text $fakeDump).UpdatesStopped) {
        throw 'Simulated-battery parser self-test failed.'
    }

    $processStat = '2542 (mu.azahar.debug) S 1870 1870 0 0 -1 1077936448 441662 0 103 0 13325 3739 0 0'
    if ((Get-ProcessCpuTicksFromStat -Text $processStat) -ne 17064) {
        throw 'Process-stat parser self-test failed.'
    }
    $gpuBusy = Get-GpuBusyPercent -Text '82469 1003316'
    if ([Math]::Abs($gpuBusy - (100.0 * 82469.0 / 1003316.0)) -gt 0.000001) {
        throw 'KGSL busy parser self-test failed.'
    }
    $powerFixture = @(
        '-1200000',
        '4200000',
        '5040000',
        '5000000',
        '4000000',
        '315',
        '70',
        'Discharging',
        '0',
        '0',
        '0',
        '401',
        '82469 1003316',
        $processStat
    ) -join "`n"
    $powerSample = ConvertFrom-ThorPowerText -Text $powerFixture -ElapsedSeconds 1.25
    if ($powerSample.Watts -ne 5.04 -or $powerSample.DerivedWatts -ne 5.04 -or
        $powerSample.TemperatureC -ne 31.5 -or $powerSample.ProcessCpuTicks -ne 17064 -or
        $powerSample.GpuClockMhz -ne 401 -or $powerSample.ChargeCounterMicroAmpHours -ne 4000000) {
        throw 'Complete Thor power-sample parser self-test failed.'
    }

    Write-Host 'measure-thor-power.ps1 self-test passed.'
}

function Resolve-AdbExecutable {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "ADB executable not found: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $sdkRoots = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique
    foreach ($sdkRoot in $sdkRoots) {
        $candidate = Join-Path $sdkRoot 'platform-tools\adb.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command adb -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw 'ADB was not found. Set ANDROID_HOME/ANDROID_SDK_ROOT or pass -AdbPath.'
    }
    return $command.Source
}

function Invoke-AdbText {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $output = & $script:ResolvedAdb @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($Arguments -join ' ') failed: $($output -join ' ')"
    }
    return (($output | ForEach-Object { "$_" }) -join "`n").Trim()
}

function Get-DeviceBatteryState {
    $dump = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'dumpsys', 'battery')
    return ConvertFrom-BatteryDump -Text $dump
}

function Assert-RealDischargeState {
    param(
        [Parameter(Mandatory)]
        [object]$State,
        [Parameter(Mandatory)]
        [string]$Phase
    )

    if ($State.UpdatesStopped) {
        throw "${Phase}: Android battery updates are simulated/stopped. Run 'adb shell dumpsys battery reset', physically unplug, and retry."
    }
    if ($State.AcPowered -or $State.UsbPowered -or $State.WirelessPowered) {
        throw "${Phase}: external power is connected. This run is invalid for a battery-watt claim."
    }
    if ($State.Status -ne 3) {
        throw "${Phase}: battery status is $($State.Status), not Android DISCHARGING (3)."
    }
}

function ConvertTo-Int64Invariant {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [Parameter(Mandatory)]
        [string]$Name
    )

    $value = 0L
    if (-not [long]::TryParse(
            $Text.Trim(),
            [System.Globalization.NumberStyles]::Integer,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        throw "Invalid $Name value from Thor: '$Text'"
    }
    return $value
}

function Get-ProcessCpuTicksFromStat {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    # The process name is parenthesized and may contain spaces. The captured fields begin at
    # proc(5) field 4, so indexes 10 and 11 are utime and stime (fields 14 and 15).
    $match = [regex]::Match($Text.Trim(), '^\d+\s+\(.+\)\s+\S\s+(.+)$')
    if (-not $match.Success) {
        throw "Invalid /proc process stat line: '$Text'"
    }
    $fields = @($match.Groups[1].Value -split '\s+')
    if ($fields.Count -lt 12) {
        throw "Incomplete /proc process stat line: '$Text'"
    }
    $userTicks = ConvertTo-Int64Invariant -Text $fields[10] -Name 'process utime'
    $systemTicks = ConvertTo-Int64Invariant -Text $fields[11] -Name 'process stime'
    return $userTicks + $systemTicks
}

function Get-GpuBusyPercent {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    $match = [regex]::Match($Text.Trim(), '^(\d+)\s+(\d+)$')
    if (-not $match.Success) {
        throw "Invalid KGSL gpubusy value: '$Text'"
    }
    $busy = [double]$match.Groups[1].Value
    $total = [double]$match.Groups[2].Value
    if ($total -le 0.0 -or $busy -lt 0.0 -or $busy -gt $total) {
        throw "Implausible KGSL gpubusy value: '$Text'"
    }
    return 100.0 * $busy / $total
}

function ConvertFrom-ThorPowerText {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [Parameter(Mandatory)]
        [double]$ElapsedSeconds
    )

    $lines = @($text -split "`r?`n")
    if ($lines.Count -ne 14) {
        throw "Expected 14 Thor power values, received $($lines.Count)."
    }

    $currentMicroAmps = ConvertTo-Int64Invariant -Text $lines[0] -Name 'current_now'
    $voltageMicroVolts = ConvertTo-Int64Invariant -Text $lines[1] -Name 'voltage_now'
    $powerMicroWatts = ConvertTo-Int64Invariant -Text $lines[2] -Name 'power_now'
    $averagePowerMicroWatts = ConvertTo-Int64Invariant -Text $lines[3] -Name 'power_avg'
    $chargeCounterMicroAmpHours = ConvertTo-Int64Invariant -Text $lines[4] -Name 'charge_counter'
    $temperatureTenthsC = ConvertTo-Int64Invariant -Text $lines[5] -Name 'temp'
    $capacityPercent = ConvertTo-Int64Invariant -Text $lines[6] -Name 'capacity'
    $usbOnline = ConvertTo-Int64Invariant -Text $lines[8] -Name 'usb online'
    $wirelessOnline = ConvertTo-Int64Invariant -Text $lines[9] -Name 'wireless online'
    $ucsiOnline = ConvertTo-Int64Invariant -Text $lines[10] -Name 'UCSI source online'
    $gpuClockMhz = ConvertTo-Int64Invariant -Text $lines[11] -Name 'KGSL clock_mhz'
    $gpuBusyPercent = Get-GpuBusyPercent -Text $lines[12]
    $processCpuTicks = Get-ProcessCpuTicksFromStat -Text $lines[13]

    if ($usbOnline -ne 0 -or $wirelessOnline -ne 0 -or $ucsiOnline -ne 0) {
        throw 'A charger came online during the measurement; discarding the run.'
    }

    $directWatts = [Math]::Abs([double]$powerMicroWatts) / 1000000.0
    $derivedWatts = [Math]::Abs([double]$currentMicroAmps) *
        [Math]::Abs([double]$voltageMicroVolts) / 1000000000000.0
    if ($directWatts -le 0.0 -or $directWatts -gt 30.0) {
        if ($derivedWatts -le 0.0 -or $derivedWatts -gt 30.0) {
            throw "Thor returned implausible power_now ($directWatts W) and derived power ($derivedWatts W)."
        }
        $selectedWatts = $derivedWatts
        $powerSource = 'current_now*voltage_now'
    } else {
        $selectedWatts = $directWatts
        $powerSource = 'power_now'
    }

    return [pscustomobject]@{
        TimestampUtc = [DateTime]::UtcNow.ToString('o')
        ElapsedSeconds = [Math]::Round($ElapsedSeconds, 3)
        Watts = [Math]::Round($selectedWatts, 6)
        PowerSource = $powerSource
        PowerNowMicroWatts = $powerMicroWatts
        PowerAverageMicroWatts = $averagePowerMicroWatts
        ChargeCounterMicroAmpHours = $chargeCounterMicroAmpHours
        DerivedWatts = [Math]::Round($derivedWatts, 6)
        CurrentMicroAmps = $currentMicroAmps
        VoltageMicroVolts = $voltageMicroVolts
        TemperatureC = [Math]::Round([double]$temperatureTenthsC / 10.0, 1)
        CapacityPercent = $capacityPercent
        BatteryStatus = $lines[7].Trim()
        UsbOnline = $usbOnline
        WirelessOnline = $wirelessOnline
        UcsiSourceOnline = $ucsiOnline
        GpuClockMhz = $gpuClockMhz
        GpuBusyPercent = [Math]::Round($gpuBusyPercent, 6)
        ProcessCpuTicks = $processCpuTicks
    }
}

function Get-ThorPowerSample {
    param(
        [Parameter(Mandatory)]
        [double]$ElapsedSeconds
    )

    $paths = @(
        '/sys/class/power_supply/battery/current_now',
        '/sys/class/power_supply/battery/voltage_now',
        '/sys/class/power_supply/battery/power_now',
        '/sys/class/power_supply/battery/power_avg',
        '/sys/class/power_supply/battery/charge_counter',
        '/sys/class/power_supply/battery/temp',
        '/sys/class/power_supply/battery/capacity',
        '/sys/class/power_supply/battery/status',
        '/sys/class/power_supply/usb/online',
        '/sys/class/power_supply/wireless/online',
        '/sys/class/power_supply/ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1/online',
        '/sys/class/kgsl/kgsl-3d0/clock_mhz',
        '/sys/class/kgsl/kgsl-3d0/gpubusy',
        "/proc/$devicePid/stat"
    )
    $text = Invoke-AdbText -Arguments (@('-s', $Serial, 'shell', 'cat') + $paths)
    return ConvertFrom-ThorPowerText -Text $text -ElapsedSeconds $ElapsedSeconds
}

function Get-RemoteSetting {
    param([string]$Name)
    return (Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'settings', 'get', 'system', $Name)).Trim()
}

function Save-DeviceScreenshot {
    param(
        [Parameter(Mandatory)]
        [string]$Destination
    )

    $remotePath = '/sdcard/Download/.azahar-thor-power-gate.png'
    try {
        [void](Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'screencap', '-p', $remotePath))
        [void](Invoke-AdbText -Arguments @('-s', $Serial, 'pull', $remotePath, $Destination))
    } finally {
        try {
            [void](Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'rm', '-f', $remotePath))
        } catch {
            Write-Warning "Could not remove temporary device screenshot: $_"
        }
    }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

if ($RequireWifiAdb -and $Serial -notmatch '^[^:]+:\d+$') {
    throw "Serial '$Serial' is not a Wi-Fi ADB host:port endpoint."
}

$script:ResolvedAdb = Resolve-AdbExecutable -RequestedPath $AdbPath
$deviceState = Invoke-AdbText -Arguments @('-s', $Serial, 'get-state')
if ($deviceState -ne 'device') {
    throw "ADB endpoint $Serial is not ready: $deviceState"
}

$model = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'getprop', 'ro.product.model')
if ($model -notmatch '^AYN Thor') {
    throw "Expected AYN Thor hardware, found '$model'."
}

$devicePid = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'pidof', $Package)
if ([string]::IsNullOrWhiteSpace($devicePid)) {
    throw "Package $Package is not running. Launch the fixed test scene before measuring."
}

$packageDump = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'dumpsys', 'package', $Package)
$versionMatch = [regex]::Match($packageDump, '(?m)^\s*versionName=(.+)$')
if (-not $versionMatch.Success) {
    throw "Could not read the installed $Package version."
}
$versionName = $versionMatch.Groups[1].Value.Trim()
if ($ExpectedVersionName -and $versionName -ne $ExpectedVersionName) {
    throw "Installed version is $versionName; expected $ExpectedVersionName."
}
if ($packageDump -match '(?m)^\s*(?:pkg)?flags=\[[^\]]*\bDEBUGGABLE\b') {
    throw 'The installed package is debuggable and is invalid for a production power gate.'
}
$abiMatch = [regex]::Match($packageDump, '(?m)^\s*primaryCpuAbi=(\S+)\s*$')
if (-not $abiMatch.Success -or $abiMatch.Groups[1].Value -ne 'arm64-v8a') {
    throw "Installed primary ABI is '$($abiMatch.Groups[1].Value)'; expected arm64-v8a."
}

$performanceMode = [int](Get-RemoteSetting -Name 'performance_mode')
$fanMode = [int](Get-RemoteSetting -Name 'fan_mode')
$brightness = [int](Get-RemoteSetting -Name 'screen_brightness')
if ($performanceMode -ne $ExpectedPerformanceMode) {
    throw "Performance mode is $performanceMode; expected $ExpectedPerformanceMode. Change it outside Azahar and retry."
}
if ($fanMode -ne $ExpectedFanMode) {
    throw "Fan mode is $fanMode; expected $ExpectedFanMode. Change it outside Azahar and retry."
}
if ($ExpectedBrightness -ge 0 -and $brightness -ne $ExpectedBrightness) {
    throw "Brightness is $brightness; expected $ExpectedBrightness."
}

$configHashLine = Invoke-AdbText -Arguments @(
    '-s', $Serial, 'shell', 'sha256sum', '/sdcard/Azaharuser/config/config.ini'
)
$configHash = ($configHashLine -split '\s+')[0].ToUpperInvariant()
if ($ExpectedConfigSha256 -and $configHash -ne $ExpectedConfigSha256.ToUpperInvariant()) {
    throw "Config SHA-256 is $configHash; expected $($ExpectedConfigSha256.ToUpperInvariant())."
}

$initialBatteryState = Get-DeviceBatteryState
Assert-RealDischargeState -State $initialBatteryState -Phase 'Preflight'

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $repositoryRoot = Split-Path -Parent $PSScriptRoot
    $OutputDirectory = Join-Path $repositoryRoot (
        'thor-power-results\' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
    )
}
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

$shouldCapture = $CaptureScreenshots -or -not [string]::IsNullOrWhiteSpace($ExpectedScreenshotSha256)
$beforeScreenshotHash = ''
$afterScreenshotHash = ''
if ($shouldCapture) {
    $beforeScreenshotHash = Save-DeviceScreenshot -Destination (
        Join-Path $OutputDirectory 'before.png'
    )
    if ($ExpectedScreenshotSha256 -and
        $beforeScreenshotHash -ne $ExpectedScreenshotSha256.ToUpperInvariant()) {
        throw "Before screenshot SHA-256 is $beforeScreenshotHash; expected $($ExpectedScreenshotSha256.ToUpperInvariant())."
    }
}

Write-Host "Warming $model / $Package for $WarmupSeconds seconds on battery..."
$warmupTimer = [System.Diagnostics.Stopwatch]::StartNew()
while ($warmupTimer.Elapsed.TotalSeconds -lt $WarmupSeconds) {
    $remaining = $WarmupSeconds - $warmupTimer.Elapsed.TotalSeconds
    Start-Sleep -Milliseconds ([int](1000.0 * [Math]::Min(1.0, $remaining)))
}
$warmupTimer.Stop()
Assert-RealDischargeState -State (Get-DeviceBatteryState) -Phase 'After warmup'
if ([string]::IsNullOrWhiteSpace(
        (Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'pidof', $Package)))) {
    throw "$Package exited during warmup."
}

$samples = [System.Collections.Generic.List[object]]::new()
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$nextSampleSeconds = 0.0
Write-Host "Sampling for $DurationSeconds seconds; mean and P95 must each be <= $MaxAverageWatts / $MaxP95Watts W..."
while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
    $remaining = $nextSampleSeconds - $timer.Elapsed.TotalSeconds
    if ($remaining -gt 0.0) {
        Start-Sleep -Milliseconds ([int](1000.0 * $remaining))
    }
    $samples.Add((Get-ThorPowerSample -ElapsedSeconds $timer.Elapsed.TotalSeconds))
    $nextSampleSeconds += $IntervalSeconds
}
$timer.Stop()

$minimumSamples = [Math]::Max(10, [Math]::Floor(0.8 * $DurationSeconds / $IntervalSeconds))
if ($samples.Count -lt $minimumSamples) {
    throw "Only $($samples.Count) samples were captured; expected at least $minimumSamples."
}

Assert-RealDischargeState -State (Get-DeviceBatteryState) -Phase 'Postflight'
$postflightPid = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'pidof', $Package)
if ([string]::IsNullOrWhiteSpace($postflightPid)) {
    throw "$Package exited during measurement."
}
if ($postflightPid -ne $devicePid) {
    throw "$Package restarted during measurement (PID $devicePid -> $postflightPid)."
}

if ($shouldCapture) {
    $afterScreenshotHash = Save-DeviceScreenshot -Destination (
        Join-Path $OutputDirectory 'after.png'
    )
    if ($ExpectedScreenshotSha256 -and
        $afterScreenshotHash -ne $ExpectedScreenshotSha256.ToUpperInvariant()) {
        throw "After screenshot SHA-256 is $afterScreenshotHash; expected $($ExpectedScreenshotSha256.ToUpperInvariant())."
    }
}

$powerStatistics = Get-SampleStatistics -Values ([double[]]@($samples | ForEach-Object { $_.Watts }))
$temperatureStatistics = Get-SampleStatistics -Values (
    [double[]]@($samples | ForEach-Object { $_.TemperatureC })
)
$thermalSlope = Get-ThermalSlopePerMinute -Samples $samples.ToArray()
$passed = Test-PowerGate -Statistics $powerStatistics -MaximumMean $MaxAverageWatts -MaximumP95 $MaxP95Watts
$gpuBusyStatistics = Get-SampleStatistics -Values (
    [double[]]@($samples | ForEach-Object { $_.GpuBusyPercent })
)
$gpuClockStatistics = Get-SampleStatistics -Values (
    [double[]]@($samples | ForEach-Object { $_.GpuClockMhz })
)
$processCpuTicks = [long]$samples[$samples.Count - 1].ProcessCpuTicks -
    [long]$samples[0].ProcessCpuTicks
$processCpuTicksPerSecond = $processCpuTicks / [double]$timer.Elapsed.TotalSeconds
$chargeCounterDelta = [long]$samples[$samples.Count - 1].ChargeCounterMicroAmpHours -
    [long]$samples[0].ChargeCounterMicroAmpHours
$meanVoltageMicroVolts = (
    $samples | Measure-Object -Property VoltageMicroVolts -Average
).Average
$chargeDerivedWattHours = -1.0 * $chargeCounterDelta * $meanVoltageMicroVolts / 1000000000000.0
$chargeDerivedAverageWatts =
    $chargeDerivedWattHours * 3600.0 / [double]$timer.Elapsed.TotalSeconds

$samplesPath = Join-Path $OutputDirectory 'samples.csv'
$summaryPath = Join-Path $OutputDirectory 'summary.json'
$samples | Export-Csv -NoTypeInformation -Encoding UTF8 -LiteralPath $samplesPath

$summary = [pscustomobject]@{
    Passed = $passed
    Gate = [pscustomobject]@{
        MaxAverageWatts = $MaxAverageWatts
        MaxP95Watts = $MaxP95Watts
    }
    Power = $powerStatistics
    Temperature = [pscustomobject]@{
        Statistics = $temperatureStatistics
        SlopeCPerMinute = $thermalSlope
    }
    Workload = [pscustomobject]@{
        ProcessCpuTicks = $processCpuTicks
        ProcessCpuTicksPerSecond = $processCpuTicksPerSecond
        GpuBusyPercent = $gpuBusyStatistics
        GpuClockMhz = $gpuClockStatistics
        ChargeCounterDeltaMicroAmpHours = $chargeCounterDelta
        ChargeDerivedWattHours = $chargeDerivedWattHours
        ChargeDerivedAverageWatts = $chargeDerivedAverageWatts
    }
    Run = [pscustomobject]@{
        StartedUtc = $samples[0].TimestampUtc
        DurationSeconds = $timer.Elapsed.TotalSeconds
        IntervalSeconds = $IntervalSeconds
        WarmupSeconds = $WarmupSeconds
        SampleCount = $samples.Count
    }
    Device = [pscustomobject]@{
        Serial = $Serial
        Model = $model
        Package = $Package
        Pid = $devicePid
        VersionName = $versionName
        PerformanceMode = $performanceMode
        FanMode = $fanMode
        Brightness = $brightness
        ConfigSha256 = $configHash
        BeforeScreenshotSha256 = $beforeScreenshotHash
        AfterScreenshotSha256 = $afterScreenshotHash
    }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $summaryPath

$powerSummary = 'Power: mean={0:N3} W median={1:N3} W P95={2:N3} W max={3:N3} W' -f $powerStatistics.Mean, $powerStatistics.Median, $powerStatistics.P95, $powerStatistics.Maximum
$temperatureSummary = 'Temperature: {0:N1}-{1:N1} C, slope={2:N3} C/min' -f $temperatureStatistics.Minimum, $temperatureStatistics.Maximum, $thermalSlope
Write-Host $powerSummary
Write-Host $temperatureSummary
Write-Host "Raw samples: $samplesPath"
Write-Host "Summary: $summaryPath"

if (-not $passed) {
    Write-Error "Thor power gate failed: mean and P95 must remain within their configured limits."
    exit 2
}

Write-Host 'Thor power gate passed.'
