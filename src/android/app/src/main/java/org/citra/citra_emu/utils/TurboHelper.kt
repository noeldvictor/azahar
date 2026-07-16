// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntSetting

object TurboHelper {
    private var turboSpeedEnabled = false

    fun isTurboSpeedEnabled(): Boolean = turboSpeedEnabled

    fun reloadTurbo(@Suppress("UNUSED_PARAMETER") showToast: Boolean) {
        if (turboSpeedEnabled) {
            NativeLibrary.setTemporaryFrameLimit(IntSetting.TURBO_LIMIT.int.toDouble())
        } else {
            NativeLibrary.disableTemporaryFrameLimit()
        }
    }

    fun setTurboEnabled(state: Boolean, showToast: Boolean) {
        turboSpeedEnabled = state
        reloadTurbo(showToast)
    }

    fun toggleTurbo(showToast: Boolean) {
        setTurboEnabled(!TurboHelper.isTurboSpeedEnabled(), showToast)
    }
}
