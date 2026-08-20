// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

package org.citra.citra_emu.features.cheats.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MemorySearchValueTest {
    @Test
    fun `parses visible decimal and advanced hexadecimal values`() {
        assertEquals(12_345L, parseMemorySearchValue("12_345", 4))
        assertEquals(255L, parseMemorySearchValue("0xFF", 1))
    }

    @Test
    fun `rejects missing negative and out of range values`() {
        assertNull(parseMemorySearchValue("", 4))
        assertNull(parseMemorySearchValue("-1", 4))
        assertNull(parseMemorySearchValue("256", 1))
        assertNull(parseMemorySearchValue("0x10000", 2))
        assertNull(parseMemorySearchValue("1", 3))
    }

    @Test
    fun `accepts full unsigned 32 bit range`() {
        assertEquals(0xFFFFFFFFL, parseMemorySearchValue("4294967295", 4))
        assertNull(parseMemorySearchValue("4294967296", 4))
    }

    @Test
    fun `creates gateway codes for every supported number size`() {
        val address = 0x81234567L
        assertEquals("21234567 000000AB", memorySearchGatewayCode(address, 0xABL, 1))
        assertEquals("11234567 0000ABCD", memorySearchGatewayCode(address, 0xABCDL, 2))
        assertEquals("01234567 ABCDEF01", memorySearchGatewayCode(address, 0xABCDEF01L, 4))
    }

    @Test(expected = IllegalStateException::class)
    fun `rejects unsupported gateway number size`() {
        memorySearchGatewayCode(0, 0, 3)
    }
}
