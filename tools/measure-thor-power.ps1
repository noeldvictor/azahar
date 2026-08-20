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
    [ValidateRange(0.0, 100000.0)]
    [double]$MinProcessCpuTicksPerSecond = 10.0,
    [ValidateRange(0.0, 100.0)]
    [double]$MinMeanGpuBusyPercent = 1.0,
    [ValidateRange(1, 8)]
    [int]$ExpectedSurfaceLayerCount = 2,
    [ValidateRange(1, 8)]
    [int]$MinMeasuredSurfaceLayerCount = 1,
    [ValidateRange(1, 126)]
    [int]$MinPresentIntervalsPerMeasuredLayer = 60,
    [ValidateRange(1.0, 240.0)]
    [double]$MinMeanPresentedFps = 29.0,
    [ValidateRange(1.0, 1000.0)]
    [double]$MaxP95PresentIntervalMs = 40.0,
    [ValidateRange(0, 10000)]
    [int]$MaxPresentIntervalsOver50Ms = 0,
    [ValidateRange(1, 384000)]
    [int]$ExpectedAudioSampleRate = 32728,
    [ValidateRange(1, 96000)]
    [int]$MaxAudioFrameCount = 2048,
    [ValidateRange(1.0, 5000.0)]
    [double]$MaxAudioLatencyMs = 150.0,
    [ValidateRange(0, 1000000)]
    [int]$MaxAudioUnderruns = 0,
    [int]$ExpectedPerformanceMode = 0,
    [int]$ExpectedFanMode = 4,
    [int]$ExpectedBrightness = -1,
    [int]$ExpectedBrightnessMode = 0,
    [ValidateRange(1, 8)]
    [int]$ExpectedActiveDisplayCount = 2,
    [string]$ExpectedVersionName = 'bc25ea052-vanilla-thor',
    [string]$ExpectedVulkanDriverName = 'Mesa Turnip driver v26.0.0 - R8',
    [string]$ExpectedVulkanDriverVersion = 'Vulkan 1.4.335',
    [string]$ExpectedVulkanDriverLibraryName = 'vulkan.ad07xx.so',
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

function Test-WorkloadGate {
    param(
        [Parameter(Mandatory)]
        [double]$ProcessCpuTicksPerSecond,
        [Parameter(Mandatory)]
        [double]$MeanGpuBusyPercent,
        [Parameter(Mandatory)]
        [double]$MinimumProcessCpuTicksPerSecond,
        [Parameter(Mandatory)]
        [double]$MinimumMeanGpuBusyPercent
    )

    return $ProcessCpuTicksPerSecond -ge $MinimumProcessCpuTicksPerSecond -and
        $MeanGpuBusyPercent -ge $MinimumMeanGpuBusyPercent
}

function ConvertFrom-SurfaceFlingerLatency {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [Parameter(Mandatory)]
        [string]$Layer
    )

    $lines = @($Text -split "`r?`n")
    if ($lines.Count -eq 0) {
        throw "SurfaceFlinger returned no latency data for '$Layer'."
    }
    $refreshPeriodNanoseconds = ConvertTo-Int64Invariant -Text $lines[0] -Name 'refresh period'
    $timestamps = [System.Collections.Generic.List[long]]::new()
    foreach ($line in ($lines | Select-Object -Skip 1)) {
        $match = [regex]::Match($line, '^\s*(\d+)\s+(\d+)\s+(\d+)\s*$')
        if (-not $match.Success) {
            continue
        }
        $timestamp = ConvertTo-Int64Invariant -Text $match.Groups[1].Value -Name 'present timestamp'
        if ($timestamp -gt 0 -and $timestamp -lt [long]::MaxValue) {
            $timestamps.Add($timestamp)
        }
    }

    $intervals = [System.Collections.Generic.List[double]]::new()
    for ($index = 1; $index -lt $timestamps.Count; $index++) {
        $deltaNanoseconds = $timestamps[$index] - $timestamps[$index - 1]
        if ($deltaNanoseconds -gt 0) {
            $intervals.Add([double]$deltaNanoseconds / 1000000.0)
        }
    }

    if ($intervals.Count -eq 0) {
        return [pscustomobject]@{
            Layer = $Layer
            Measurable = $false
            RefreshPeriodNanoseconds = $refreshPeriodNanoseconds
            FrameCount = $timestamps.Count
            IntervalCount = 0
            MeanPresentedFps = 0.0
            IntervalMilliseconds = $null
            IntervalsOver50Ms = 0
        }
    }

    $statistics = Get-SampleStatistics -Values ([double[]]$intervals.ToArray())
    return [pscustomobject]@{
        Layer = $Layer
        Measurable = $true
        RefreshPeriodNanoseconds = $refreshPeriodNanoseconds
        FrameCount = $timestamps.Count
        IntervalCount = $intervals.Count
        MeanPresentedFps = 1000.0 / $statistics.Mean
        IntervalMilliseconds = $statistics
        IntervalsOver50Ms = @($intervals | Where-Object { $_ -gt 50.0 }).Count
    }
}

function Test-SurfacePacingSnapshot {
    param(
        [Parameter(Mandatory)]
        [object]$Snapshot
    )

    if ($Snapshot.LayerCount -ne $ExpectedSurfaceLayerCount -or
        $Snapshot.MeasuredLayerCount -lt $MinMeasuredSurfaceLayerCount) {
        return $false
    }
    foreach ($layer in $Snapshot.Layers) {
        if (-not $layer.Measurable) {
            continue
        }
        if ($layer.IntervalCount -lt $MinPresentIntervalsPerMeasuredLayer -or
            $layer.MeanPresentedFps -lt $MinMeanPresentedFps -or
            $layer.IntervalMilliseconds.P95 -gt $MaxP95PresentIntervalMs -or
            $layer.IntervalsOver50Ms -gt $MaxPresentIntervalsOver50Ms) {
            return $false
        }
    }
    return $true
}

function ConvertFrom-AudioFlingerDump {
    param(
        [Parameter(Mandatory)]
        [string]$Text,
        [Parameter(Mandatory)]
        [int]$ProcessId
    )

    $trackPattern = '^\s*\d+\s+yes\s+' + [regex]::Escape("$ProcessId") + '\s+'
    $trackLines = @($Text -split "`r?`n" | Where-Object { $_ -match $trackPattern })
    if ($trackLines.Count -ne 1) {
        throw "Expected one active AudioFlinger track for PID $ProcessId, found $($trackLines.Count)."
    }
    $fields = @($trackLines[0].Trim() -split '\s+')
    if ($fields.Count -lt 24) {
        throw "Incomplete AudioFlinger track for PID ${ProcessId}: '$($trackLines[0])'"
    }
    $frameCountMatch = [regex]::Match($fields[18], '^(\d+)[A-Za-z]*$')
    if (-not $frameCountMatch.Success) {
        throw "Invalid AudioFlinger frame count '$($fields[18])'."
    }
    $latencyMilliseconds = 0.0
    if (-not [double]::TryParse(
            $fields[23],
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$latencyMilliseconds)) {
        throw "Invalid AudioFlinger latency '$($fields[23])'."
    }
    return [pscustomobject]@{
        TrackId = ConvertTo-Int64Invariant -Text $fields[0] -Name 'AudioFlinger track id'
        ProcessId = ConvertTo-Int64Invariant -Text $fields[2] -Name 'AudioFlinger client pid'
        SampleRate = ConvertTo-Int64Invariant -Text $fields[9] -Name 'AudioFlinger sample rate'
        FrameCount = ConvertTo-Int64Invariant `
            -Text $frameCountMatch.Groups[1].Value -Name 'AudioFlinger frame count'
        FramesReady = ConvertTo-Int64Invariant -Text $fields[19] -Name 'AudioFlinger frames ready'
        Underruns = ConvertTo-Int64Invariant -Text $fields[21] -Name 'AudioFlinger underruns'
        LatencyMilliseconds = $latencyMilliseconds
    }
}

function Test-AudioState {
    param(
        [Parameter(Mandatory)]
        [object]$State
    )

    return $State.SampleRate -eq $ExpectedAudioSampleRate -and
        $State.FrameCount -le $MaxAudioFrameCount -and
        $State.LatencyMilliseconds -le $MaxAudioLatencyMs -and
        $State.Underruns -le $MaxAudioUnderruns
}

function ConvertFrom-VulkanDriverMetadataLog {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    $metadataMatches = [regex]::Matches(
        $Text,
        '(?m)Active Vulkan driver metadata:\s*(\{[^\r\n]+\})\s*$'
    )
    if ($metadataMatches.Count -eq 0) {
        throw 'Active Vulkan driver metadata was not found in the current process log.'
    }

    $json = $metadataMatches[$metadataMatches.Count - 1].Groups[1].Value
    try {
        $metadata = $json | ConvertFrom-Json
    } catch {
        throw "Active Vulkan driver metadata is invalid JSON: $json"
    }
    foreach ($propertyName in @('name', 'version', 'libraryName')) {
        if ($metadata.PSObject.Properties.Name -notcontains $propertyName) {
            throw "Active Vulkan driver metadata is missing '$propertyName': $json"
        }
    }

    return [pscustomobject]@{
        Name = [string]$metadata.name
        Version = [string]$metadata.version
        LibraryName = [string]$metadata.libraryName
    }
}

function ConvertFrom-DisplayStateDump {
    param(
        [Parameter(Mandatory)]
        [string]$Text
    )

    $sectionMatch = [regex]::Match(
        $Text,
        '(?ms)^Display States: size=(\d+)\s*$\s*(.*?)^Display Adapters:'
    )
    if (-not $sectionMatch.Success) {
        throw 'Display States section was not found in dumpsys display output.'
    }

    $declaredCount = [int]$sectionMatch.Groups[1].Value
    $displayMatches = [regex]::Matches(
        $sectionMatch.Groups[2].Value,
        '(?m)^\s*Display Id=(\d+)\s*\r?\n' +
        '\s*Display State=(\S+)\s*\r?\n' +
        '\s*Display Brightness=([^\s]+)\s*\r?\n' +
        '\s*Display SdrBrightness=([^\s]+)\s*$'
    )
    $displays = [System.Collections.Generic.List[object]]::new()
    foreach ($match in $displayMatches) {
        $brightness = 0.0
        $sdrBrightness = 0.0
        foreach ($value in @(
                [pscustomobject]@{ Text = $match.Groups[3].Value; Name = 'display brightness'; Target = [ref]$brightness },
                [pscustomobject]@{ Text = $match.Groups[4].Value; Name = 'display SDR brightness'; Target = [ref]$sdrBrightness }
            )) {
            if (-not [double]::TryParse(
                    $value.Text,
                    [System.Globalization.NumberStyles]::Float,
                    [System.Globalization.CultureInfo]::InvariantCulture,
                    $value.Target)) {
                throw "Invalid $($value.Name) in dumpsys display output: '$($value.Text)'"
            }
        }
        $displays.Add([pscustomobject]@{
            Id = [int]$match.Groups[1].Value
            State = $match.Groups[2].Value
            Brightness = $brightness
            SdrBrightness = $sdrBrightness
        })
    }

    if ($displays.Count -ne $declaredCount) {
        throw "Parsed $($displays.Count) display states; dumpsys declared $declaredCount."
    }
    return [pscustomobject]@{
        Count = $displays.Count
        Displays = $displays.ToArray()
    }
}

function Assert-DisplayStateSnapshot {
    param(
        [Parameter(Mandatory)]
        [object]$Snapshot,
        [Parameter(Mandatory)]
        [string]$Phase,
        [object]$Reference = $null
    )

    if ($Snapshot.Count -ne $ExpectedActiveDisplayCount) {
        throw "${Phase}: found $($Snapshot.Count) displays; expected $ExpectedActiveDisplayCount."
    }
    foreach ($display in $Snapshot.Displays) {
        if ($display.State -ne 'ON') {
            throw "${Phase}: display $($display.Id) is $($display.State), not ON."
        }
        if ($display.Brightness -lt 0.0 -or $display.Brightness -gt 1.0 -or
            $display.SdrBrightness -lt 0.0 -or $display.SdrBrightness -gt 1.0) {
            throw "${Phase}: display $($display.Id) returned an invalid brightness."
        }
    }

    if ($null -eq $Reference) {
        return
    }
    foreach ($referenceDisplay in $Reference.Displays) {
        $current = @($Snapshot.Displays | Where-Object { $_.Id -eq $referenceDisplay.Id })
        if ($current.Count -ne 1) {
            throw "${Phase}: display $($referenceDisplay.Id) from preflight is missing or duplicated."
        }
        if ($current[0].State -ne $referenceDisplay.State -or
            [Math]::Abs($current[0].Brightness - $referenceDisplay.Brightness) -gt 0.000001 -or
            [Math]::Abs($current[0].SdrBrightness - $referenceDisplay.SdrBrightness) -gt 0.000001) {
            throw "${Phase}: display $($referenceDisplay.Id) state or brightness changed from preflight."
        }
    }
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
    if (-not (Test-WorkloadGate -ProcessCpuTicksPerSecond 22.7 -MeanGpuBusyPercent 8.2 `
            -MinimumProcessCpuTicksPerSecond 10.0 -MinimumMeanGpuBusyPercent 1.0)) {
        throw 'Passing workload-gate self-test failed.'
    }
    if (Test-WorkloadGate -ProcessCpuTicksPerSecond 0.0 -MeanGpuBusyPercent 0.0 `
            -MinimumProcessCpuTicksPerSecond 10.0 -MinimumMeanGpuBusyPercent 1.0) {
        throw 'Failing workload-gate self-test failed.'
    }
    $displayFixture = @'
Display States: size=2
  Display Id=0
  Display State=ON
  Display Brightness=0.38188976
  Display SdrBrightness=0.38188976
  Display Id=4
  Display State=ON
  Display Brightness=0.38188976
  Display SdrBrightness=0.38188976

Display Adapters: size=4
'@
    $displayState = ConvertFrom-DisplayStateDump -Text $displayFixture
    Assert-DisplayStateSnapshot -Snapshot $displayState -Phase 'Self-test'
    if ($displayState.Count -ne 2 -or $displayState.Displays[1].Id -ne 4 -or
        $displayState.Displays[0].Brightness -ne 0.38188976) {
        throw 'Display-state parser self-test failed.'
    }
    $changedDisplayState = ConvertFrom-DisplayStateDump -Text (
        $displayFixture.Replace('Display Brightness=0.38188976', 'Display Brightness=0.503937')
    )
    try {
        Assert-DisplayStateSnapshot -Snapshot $changedDisplayState -Phase 'Self-test changed' `
            -Reference $displayState
        throw 'Display-state drift self-test failed.'
    } catch {
        if ($_.Exception.Message -notmatch 'changed from preflight') {
            throw
        }
    }
    $pacingFixtureLines = [System.Collections.Generic.List[string]]::new()
    $pacingFixtureLines.Add('16666666')
    for ($index = 0; $index -le 64; $index++) {
        $timestamp = 1000000000L + 33333333L * $index
        $pacingFixtureLines.Add("$timestamp $($timestamp + 1000000) $($timestamp + 500000)")
    }
    $pacingFixture = $pacingFixtureLines -join "`n"
    $pacingLayer = ConvertFrom-SurfaceFlingerLatency -Text $pacingFixture -Layer 'fixture'
    $pacingSnapshot = [pscustomobject]@{
        LayerCount = 2
        MeasuredLayerCount = 1
        Layers = @(
            $pacingLayer,
            [pscustomobject]@{ Layer = 'unmeasured'; Measurable = $false }
        )
    }
    if (-not (Test-SurfacePacingSnapshot -Snapshot $pacingSnapshot) -or
        [Math]::Abs($pacingLayer.MeanPresentedFps - 30.0) -gt 0.001) {
        throw 'Passing SurfaceFlinger pacing self-test failed.'
    }
    $pacingLayer.IntervalMilliseconds.P95 = 100.0
    if (Test-SurfacePacingSnapshot -Snapshot $pacingSnapshot) {
        throw 'Failing SurfaceFlinger pacing self-test failed.'
    }
    $audioFixture = @'
           3589    yes   9639  141505    3566 A  0x000 00000001 00000003  32728  3   1  2  -inf     0     0     0  0019E36B   1962r   1819 A         0        0  117.56 t
'@
    $audioState = ConvertFrom-AudioFlingerDump -Text $audioFixture -ProcessId 9639
    if (-not (Test-AudioState -State $audioState) -or $audioState.FrameCount -ne 1962 -or
        $audioState.LatencyMilliseconds -ne 117.56) {
        throw 'Passing AudioFlinger state self-test failed.'
    }
    $audioState.FrameCount = 4096
    $audioState.LatencyMilliseconds = 271.84
    $audioState.Underruns = 989
    if (Test-AudioState -State $audioState) {
        throw 'Failing AudioFlinger state self-test failed.'
    }
    $driverFixture = @'
I/CitraNative(19705): Frontend <Info>: [GpuDriverHelper] Active Vulkan driver metadata: {"name":"Mesa Turnip driver v26.0.0 - R8 SYSMEM","version":"Vulkan 1.4.335","libraryName":"vulkan.ad07xx.so"}
I/CitraNative(19705): Frontend <Info>: [GpuDriverHelper] Active Vulkan driver metadata: {"name":"Mesa Turnip driver v26.0.0 - R8","version":"Vulkan 1.4.335","libraryName":"vulkan.ad07xx.so"}
'@
    $driverMetadata = ConvertFrom-VulkanDriverMetadataLog -Text $driverFixture
    if ($driverMetadata.Name -ne 'Mesa Turnip driver v26.0.0 - R8' -or
        $driverMetadata.Version -ne 'Vulkan 1.4.335' -or
        $driverMetadata.LibraryName -ne 'vulkan.ad07xx.so') {
        throw 'Vulkan-driver metadata parser self-test failed.'
    }
    try {
        [void](ConvertFrom-VulkanDriverMetadataLog -Text 'VK_DRIVER: turnip Mesa driver 25.99.99')
        throw 'Missing Vulkan-driver metadata self-test failed.'
    } catch {
        if ($_.Exception.Message -notmatch 'was not found') {
            throw
        }
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

function Get-VulkanDriverMetadata {
    $log = Invoke-AdbText -Arguments @(
        '-s', $Serial, 'logcat', '-d', "--pid=$devicePid", '-v', 'brief'
    )
    return ConvertFrom-VulkanDriverMetadataLog -Text $log
}

function Get-SurfacePacingSnapshot {
    $layerList = Invoke-AdbText -Arguments @(
        '-s', $Serial, 'shell', 'dumpsys', 'SurfaceFlinger', '--list'
    )
    $layerPattern = '^SurfaceView\[' + [regex]::Escape($Package) +
        '/org\.citra\.citra_emu\.activities\.EmulationActivity\]\(BLAST\)#\d+$'
    $layerNames = @($layerList -split "`r?`n" | Where-Object { $_ -match $layerPattern })
    $layers = [System.Collections.Generic.List[object]]::new()
    foreach ($layerName in $layerNames) {
        # The strict layer-name regex excludes shell quotes and metacharacters other than the
        # known SurfaceFlinger punctuation. Single quoting preserves its brackets and parentheses.
        $remoteCommand = "dumpsys SurfaceFlinger --latency '$layerName'"
        $latencyText = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', $remoteCommand)
        $layers.Add((ConvertFrom-SurfaceFlingerLatency -Text $latencyText -Layer $layerName))
    }
    $measuredLayerCount = @($layers | Where-Object { $_.Measurable }).Count
    $snapshot = [pscustomobject]@{
        TimestampUtc = [DateTime]::UtcNow.ToString('o')
        LayerCount = $layers.Count
        MeasuredLayerCount = $measuredLayerCount
        Layers = $layers.ToArray()
    }
    $snapshot | Add-Member -NotePropertyName Passed -NotePropertyValue (
        Test-SurfacePacingSnapshot -Snapshot $snapshot
    )
    return $snapshot
}

function Assert-SurfacePacingSnapshot {
    param(
        [Parameter(Mandatory)]
        [object]$Snapshot,
        [Parameter(Mandatory)]
        [string]$Phase
    )

    if (-not $Snapshot.Passed) {
        throw "${Phase}: SurfaceFlinger pacing failed. Expected $ExpectedSurfaceLayerCount layers, at least $MinMeasuredSurfaceLayerCount measurable layer(s), >= $MinPresentIntervalsPerMeasuredLayer intervals per measured layer, >= $MinMeanPresentedFps FPS, P95 <= $MaxP95PresentIntervalMs ms, and <= $MaxPresentIntervalsOver50Ms intervals over 50 ms."
    }
}

function Get-AudioState {
    $dump = Invoke-AdbText -Arguments @(
        '-s', $Serial, 'shell', 'dumpsys', 'media.audio_flinger'
    )
    return ConvertFrom-AudioFlingerDump -Text $dump -ProcessId ([int]$devicePid)
}

function Assert-AudioState {
    param(
        [Parameter(Mandatory)]
        [object]$State,
        [Parameter(Mandatory)]
        [string]$Phase
    )

    if (-not (Test-AudioState -State $State)) {
        throw "${Phase}: AudioFlinger state failed. Expected $ExpectedAudioSampleRate Hz, <= $MaxAudioFrameCount frames, <= $MaxAudioLatencyMs ms latency, and <= $MaxAudioUnderruns underruns; found $($State.SampleRate) Hz, $($State.FrameCount) frames, $($State.LatencyMilliseconds) ms, and $($State.Underruns) underruns."
    }
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

function Get-DisplayStateSnapshot {
    $dump = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'dumpsys', 'display')
    $snapshot = ConvertFrom-DisplayStateDump -Text $dump
    $snapshot | Add-Member -NotePropertyName TimestampUtc -NotePropertyValue (
        [DateTime]::UtcNow.ToString('o')
    )
    return $snapshot
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

$vulkanDriver = Get-VulkanDriverMetadata
if ($ExpectedVulkanDriverName -and $vulkanDriver.Name -ne $ExpectedVulkanDriverName) {
    throw "Active Vulkan driver is '$($vulkanDriver.Name)'; expected '$ExpectedVulkanDriverName'."
}
if ($ExpectedVulkanDriverVersion -and $vulkanDriver.Version -ne $ExpectedVulkanDriverVersion) {
    throw "Active Vulkan driver version is '$($vulkanDriver.Version)'; expected '$ExpectedVulkanDriverVersion'."
}
if ($ExpectedVulkanDriverLibraryName -and
    $vulkanDriver.LibraryName -ne $ExpectedVulkanDriverLibraryName) {
    throw "Active Vulkan driver library is '$($vulkanDriver.LibraryName)'; expected '$ExpectedVulkanDriverLibraryName'."
}

$performanceMode = [int](Get-RemoteSetting -Name 'performance_mode')
$fanMode = [int](Get-RemoteSetting -Name 'fan_mode')
$brightness = [int](Get-RemoteSetting -Name 'screen_brightness')
$brightnessMode = [int](Get-RemoteSetting -Name 'screen_brightness_mode')
if ($performanceMode -ne $ExpectedPerformanceMode) {
    throw "Performance mode is $performanceMode; expected $ExpectedPerformanceMode. Change it outside Azahar and retry."
}
if ($fanMode -ne $ExpectedFanMode) {
    throw "Fan mode is $fanMode; expected $ExpectedFanMode. Change it outside Azahar and retry."
}
if ($ExpectedBrightness -ge 0 -and $brightness -ne $ExpectedBrightness) {
    throw "Brightness is $brightness; expected $ExpectedBrightness."
}
if ($brightnessMode -ne $ExpectedBrightnessMode) {
    throw "Brightness mode is $brightnessMode; expected $ExpectedBrightnessMode (0 is manual)."
}
$displayStatePreflight = Get-DisplayStateSnapshot
Assert-DisplayStateSnapshot -Snapshot $displayStatePreflight -Phase 'Preflight'

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
$displayStateAfterWarmup = Get-DisplayStateSnapshot
Assert-DisplayStateSnapshot -Snapshot $displayStateAfterWarmup -Phase 'After warmup' `
    -Reference $displayStatePreflight
if ([string]::IsNullOrWhiteSpace(
        (Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'pidof', $Package)))) {
    throw "$Package exited during warmup."
}
$pacingBefore = Get-SurfacePacingSnapshot
Assert-SurfacePacingSnapshot -Snapshot $pacingBefore -Phase 'After warmup'
$audioBefore = Get-AudioState
Assert-AudioState -State $audioBefore -Phase 'After warmup'

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

$postflightBatteryState = Get-DeviceBatteryState
Assert-RealDischargeState -State $postflightBatteryState -Phase 'Postflight'
$displayStatePostflight = Get-DisplayStateSnapshot
Assert-DisplayStateSnapshot -Snapshot $displayStatePostflight -Phase 'Postflight' `
    -Reference $displayStatePreflight
$postflightPid = Invoke-AdbText -Arguments @('-s', $Serial, 'shell', 'pidof', $Package)
if ([string]::IsNullOrWhiteSpace($postflightPid)) {
    throw "$Package exited during measurement."
}
if ($postflightPid -ne $devicePid) {
    throw "$Package restarted during measurement (PID $devicePid -> $postflightPid)."
}
$pacingAfter = Get-SurfacePacingSnapshot
$audioAfter = Get-AudioState

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
$powerPassed = Test-PowerGate -Statistics $powerStatistics -MaximumMean $MaxAverageWatts -MaximumP95 $MaxP95Watts
$gpuBusyStatistics = Get-SampleStatistics -Values (
    [double[]]@($samples | ForEach-Object { $_.GpuBusyPercent })
)
$gpuClockStatistics = Get-SampleStatistics -Values (
    [double[]]@($samples | ForEach-Object { $_.GpuClockMhz })
)
$processCpuTicks = [long]$samples[$samples.Count - 1].ProcessCpuTicks -
    [long]$samples[0].ProcessCpuTicks
$processCpuTicksPerSecond = $processCpuTicks / [double]$timer.Elapsed.TotalSeconds
$workloadPassed = Test-WorkloadGate `
    -ProcessCpuTicksPerSecond $processCpuTicksPerSecond `
    -MeanGpuBusyPercent $gpuBusyStatistics.Mean `
    -MinimumProcessCpuTicksPerSecond $MinProcessCpuTicksPerSecond `
    -MinimumMeanGpuBusyPercent $MinMeanGpuBusyPercent
$pacingPassed = $pacingBefore.Passed -and $pacingAfter.Passed
$audioPassed = (Test-AudioState -State $audioAfter) -and
    $audioAfter.TrackId -eq $audioBefore.TrackId
$passed = $powerPassed -and $workloadPassed -and $pacingPassed -and $audioPassed
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
        MinProcessCpuTicksPerSecond = $MinProcessCpuTicksPerSecond
        MinMeanGpuBusyPercent = $MinMeanGpuBusyPercent
        ExpectedSurfaceLayerCount = $ExpectedSurfaceLayerCount
        MinMeasuredSurfaceLayerCount = $MinMeasuredSurfaceLayerCount
        MinPresentIntervalsPerMeasuredLayer = $MinPresentIntervalsPerMeasuredLayer
        MinMeanPresentedFps = $MinMeanPresentedFps
        MaxP95PresentIntervalMs = $MaxP95PresentIntervalMs
        MaxPresentIntervalsOver50Ms = $MaxPresentIntervalsOver50Ms
        ExpectedAudioSampleRate = $ExpectedAudioSampleRate
        MaxAudioFrameCount = $MaxAudioFrameCount
        MaxAudioLatencyMs = $MaxAudioLatencyMs
        MaxAudioUnderruns = $MaxAudioUnderruns
        ExpectedVulkanDriverName = $ExpectedVulkanDriverName
        ExpectedVulkanDriverVersion = $ExpectedVulkanDriverVersion
        ExpectedVulkanDriverLibraryName = $ExpectedVulkanDriverLibraryName
        ExpectedBrightnessMode = $ExpectedBrightnessMode
        ExpectedActiveDisplayCount = $ExpectedActiveDisplayCount
        PowerPassed = $powerPassed
        WorkloadPassed = $workloadPassed
        PacingPassed = $pacingPassed
        AudioPassed = $audioPassed
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
    Pacing = [pscustomobject]@{
        Before = $pacingBefore
        After = $pacingAfter
    }
    Audio = [pscustomobject]@{
        Before = $audioBefore
        After = $audioAfter
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
        VulkanDriver = $vulkanDriver
        PerformanceMode = $performanceMode
        FanMode = $fanMode
        Brightness = $brightness
        BrightnessMode = $brightnessMode
        Displays = [pscustomobject]@{
            Preflight = $displayStatePreflight
            AfterWarmup = $displayStateAfterWarmup
            Postflight = $displayStatePostflight
        }
        ConfigSha256 = $configHash
        BeforeScreenshotSha256 = $beforeScreenshotHash
        AfterScreenshotSha256 = $afterScreenshotHash
    }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $summaryPath

$powerSummary = 'Power: mean={0:N3} W median={1:N3} W P95={2:N3} W max={3:N3} W' -f $powerStatistics.Mean, $powerStatistics.Median, $powerStatistics.P95, $powerStatistics.Maximum
$temperatureSummary = 'Temperature: {0:N1}-{1:N1} C, slope={2:N3} C/min' -f $temperatureStatistics.Minimum, $temperatureStatistics.Maximum, $thermalSlope
$workloadSummary = 'Workload: process={0:N2} CPU ticks/s, GPU busy mean={1:N2}%, GPU clock mean={2:N1} MHz' -f $processCpuTicksPerSecond, $gpuBusyStatistics.Mean, $gpuClockStatistics.Mean
$pacingSummary = 'Pacing: start={0}/{1} measured layers, end={2}/{3}, passed={4}' -f $pacingBefore.MeasuredLayerCount, $pacingBefore.LayerCount, $pacingAfter.MeasuredLayerCount, $pacingAfter.LayerCount, $pacingPassed
$audioSummary = 'Audio: {0} Hz, {1} frames, {2:N2} ms, underruns={3}, passed={4}' -f $audioAfter.SampleRate, $audioAfter.FrameCount, $audioAfter.LatencyMilliseconds, $audioAfter.Underruns, $audioPassed
Write-Host $powerSummary
Write-Host $temperatureSummary
Write-Host $workloadSummary
Write-Host $pacingSummary
Write-Host $audioSummary
Write-Host "Raw samples: $samplesPath"
Write-Host "Summary: $summaryPath"

if (-not $passed) {
    Write-Error "Thor acceptance gate failed: power, active workload, frame pacing, and clean low-latency audio must all hold."
    exit 2
}

Write-Host 'Thor power gate passed.'
