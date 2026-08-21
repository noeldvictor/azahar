// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

package org.citra.citra_emu.features.cheats.ui

internal fun parseMemorySearchValue(text: String, size: Int): Long? {
    val cleaned = text.trim().replace("_", "")
    val value = if (cleaned.startsWith("0x", ignoreCase = true)) {
        cleaned.drop(2).toLongOrNull(16)
    } else {
        cleaned.toLongOrNull()
    }
    return value?.takeIf { it >= 0 && it <= memorySearchValueMask(size) }
}

internal fun memorySearchValueMask(size: Int): Long = when (size) {
    1 -> 0xFF
    2 -> 0xFFFF
    4 -> 0xFFFFFFFFL
    else -> -1
}
