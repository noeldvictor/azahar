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
        assertEquals(0xABCDEFL, parseMemorySearchValue("  0Xab_cdef  ", 4))
    }

    @Test
    fun `rejects missing negative and out of range values`() {
        assertNull(parseMemorySearchValue("", 4))
        assertNull(parseMemorySearchValue("-1", 4))
        assertNull(parseMemorySearchValue("256", 1))
        assertNull(parseMemorySearchValue("0x10000", 2))
        assertNull(parseMemorySearchValue("1", 3))
        assertNull(parseMemorySearchValue("0x", 4))
        assertNull(parseMemorySearchValue("12.5", 4))
    }

    @Test
    fun `accepts full unsigned 32 bit range`() {
        assertEquals(0xFFFFFFFFL, parseMemorySearchValue("4294967295", 4))
        assertNull(parseMemorySearchValue("4294967296", 4))
    }

    @Test
    fun `reports every supported unsigned value range`() {
        assertEquals(0xFFL, memorySearchValueMask(1))
        assertEquals(0xFFFFL, memorySearchValueMask(2))
        assertEquals(0xFFFFFFFFL, memorySearchValueMask(4))
        assertEquals(-1L, memorySearchValueMask(3))
    }

}
