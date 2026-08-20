// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.cheats.model

import androidx.annotation.Keep

@Keep
object CheatEngine {
    external fun loadCheatFile(titleId: Long)
    external fun saveCheatFile(titleId: Long)

    external fun getCheats(): Array<Cheat>

    external fun addCheat(cheat: Cheat?)
    external fun removeCheat(index: Int)
    external fun updateCheat(index: Int, newCheat: Cheat?)

    external fun getMemorySearchStatus(): Long
    external fun startMemorySearch(valueSize: Int, value: Long): Long
    external fun refineMemorySearch(comparison: Int, value: Long): Long
    external fun getMemorySearchResults(limit: Int): LongArray
    external fun getMemorySearchValueSize(): Int
    external fun writeMemorySearchResult(address: Long, value: Long): Boolean
    external fun canUndoMemorySearchWrite(): Boolean
    external fun undoMemorySearchWrite(): Int
    external fun resetMemorySearch()
}
