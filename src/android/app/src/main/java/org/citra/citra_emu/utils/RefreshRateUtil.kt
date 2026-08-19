// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.app.Activity
import android.os.Build
import android.view.Display
import android.view.Surface
import kotlin.math.abs

private const val TARGET_GAME_REFRESH_RATE = 60.0f
private const val REFRESH_RATE_TOLERANCE = 1.0f

object RefreshRateUtil {
    /**
     * Requests a refresh rate without also requesting a display resolution.
     *
     * Emulation prefers the closest current-resolution mode to 60 Hz. Frontend activities retain
     * their existing preference for the highest current-resolution refresh rate.
     */
    fun enforceRefreshRate(activity: Activity, sixtyHz: Boolean = false) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return
        }

        val display = activity.display ?: return
        val refreshRate = selectDisplayRefreshRate(display, sixtyHz) ?: return
        val attributes = activity.window.attributes
        attributes.preferredDisplayModeId = 0
        attributes.preferredRefreshRate = refreshRate
        activity.window.attributes = attributes
    }

    /** Gives Android's compositor the 60 Hz game-content hint on the actual render surface. */
    fun enforceGameSurfaceRefreshRate(surface: Surface) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || !surface.isValid) {
            return
        }

        surface.setFrameRate(
            TARGET_GAME_REFRESH_RATE,
            Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
        )
    }

    private fun selectDisplayRefreshRate(display: Display, sixtyHz: Boolean): Float? {
        val currentMode = display.mode
        val currentResolutionRefreshRates =
            display.supportedModes
                .asSequence()
                .filter {
                    it.physicalWidth == currentMode.physicalWidth &&
                        it.physicalHeight == currentMode.physicalHeight
                }
                .map { it.refreshRate }
                .toList()

        return selectRefreshRate(currentResolutionRefreshRates, sixtyHz)
    }
}

internal fun selectRefreshRate(refreshRates: List<Float>, sixtyHz: Boolean): Float? {
    val validRates = refreshRates.filter { it.isFinite() && it > 0.0f }
    if (!sixtyHz) {
        return validRates.maxOrNull()
    }

    val closestRate = validRates.minByOrNull { abs(it - TARGET_GAME_REFRESH_RATE) } ?: return null
    return closestRate.takeIf {
        abs(it - TARGET_GAME_REFRESH_RATE) <= REFRESH_RATE_TOLERANCE
    }
}
