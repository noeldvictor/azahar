// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class RefreshRateUtilTest {
    @Test
    fun `game refresh accepts fractional 60 Hz mode`() {
        assertEquals(59.94f, selectRefreshRate(listOf(120.0f, 59.94f), sixtyHz = true))
    }

    @Test
    fun `game refresh picks closest 60 Hz mode`() {
        assertEquals(60.01f, selectRefreshRate(listOf(59.5f, 60.01f), sixtyHz = true))
    }

    @Test
    fun `game refresh does not force unrelated rate`() {
        assertNull(selectRefreshRate(listOf(90.0f, 120.0f), sixtyHz = true))
    }

    @Test
    fun `frontend refresh picks highest valid rate`() {
        assertEquals(
            120.0f,
            selectRefreshRate(listOf(Float.NaN, -1.0f, 60.0f, 120.0f), sixtyHz = false),
        )
    }

    @Test
    fun `empty refresh list has no preference`() {
        assertNull(selectRefreshRate(emptyList(), sixtyHz = false))
    }
}
