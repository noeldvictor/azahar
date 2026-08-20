// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

package org.citra.citra_emu.features.cheats.ui

import java.util.Locale

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

internal fun memorySearchGatewayCode(address: Long, value: Long, size: Int): String {
    val gatewayAddress = address and 0x0FFFFFFF
    return when (size) {
        1 -> String.format(Locale.ROOT, "2%07X 000000%02X", gatewayAddress, value)
        2 -> String.format(Locale.ROOT, "1%07X 0000%04X", gatewayAddress, value)
        4 -> String.format(Locale.ROOT, "%08X %08X", gatewayAddress, value)
        else -> error("Unsupported memory-search value size")
    }
}
