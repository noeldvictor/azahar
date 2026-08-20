// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

package org.citra.citra_emu.features.cheats.ui

import android.content.DialogInterface
import android.text.InputType
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.features.cheats.model.Cheat
import org.citra.citra_emu.features.cheats.model.CheatEngine
import org.citra.citra_emu.features.cheats.model.CheatsViewModel

/** Controller-friendly UI for the paused, offline-only guest-memory search engine. */
class MemorySearchDialog(
    private val fragment: Fragment,
    private val cheatsViewModel: CheatsViewModel
) {
    private val context get() = fragment.requireContext()

    fun show() {
        when (val status = CheatEngine.getMemorySearchStatus()) {
            SEARCH_NO_SESSION, SEARCH_TITLE_CHANGED -> showStartDialog()
            SEARCH_NO_GAME -> showError(R.string.memory_search_error_no_game)
            SEARCH_ONLINE_BLOCKED -> showError(R.string.memory_search_error_online)
            SEARCH_NOT_PAUSED -> showError(R.string.memory_search_error_not_paused)
            else -> if (status >= 0) {
                showRefineMenu(status)
            } else {
                showError(R.string.memory_search_error_generic)
            }
        }
    }

    private fun showStartDialog() {
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search_start)
            .setMessage(R.string.memory_search_offline_warning)
            .setPositiveButton(R.string.memory_search_continue) { _, _ ->
                promptSearchValue(DEFAULT_VALUE_SIZE, true)
            }
            .setNeutralButton(R.string.memory_search_advanced) { _, _ -> showValueSizeChoices() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showValueSizeChoices() {
        val labels = arrayOf(
            context.getString(R.string.memory_search_size_8),
            context.getString(R.string.memory_search_size_16),
            context.getString(R.string.memory_search_size_32)
        )
        val sizes = intArrayOf(1, 2, 4)
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search_choose_size)
            .setItems(labels) { _, which -> promptSearchValue(sizes[which], true) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showRefineMenu(candidateCount: Long) {
        val actions = mutableListOf(
            Action(R.string.memory_search_how_to_refine) { showRefineHelp(candidateCount) },
            Action(R.string.memory_search_next_exact) { promptSearchValue(valueSize(), false) },
            Action(R.string.memory_search_changed) { refine(COMPARISON_CHANGED) },
            Action(R.string.memory_search_unchanged) { refine(COMPARISON_UNCHANGED) },
            Action(R.string.memory_search_increased) { refine(COMPARISON_INCREASED) },
            Action(R.string.memory_search_decreased) { refine(COMPARISON_DECREASED) }
        )
        if (candidateCount > 0) {
            actions += Action(R.string.memory_search_view_results) { showResults() }
        }
        if (CheatEngine.canUndoMemorySearchWrite()) {
            actions += Action(R.string.memory_search_undo_write) { undoWrite() }
        }
        actions += Action(R.string.memory_search_new) {
            CheatEngine.resetMemorySearch()
            showStartDialog()
        }

        MaterialAlertDialogBuilder(context)
            .setTitle(context.getString(R.string.memory_search_candidates, candidateCount))
            .setItems(actions.map { context.getString(it.label) }.toTypedArray()) { _, which ->
                actions[which].run()
            }
            .setPositiveButton(R.string.memory_search_back_to_game) { _, _ -> returnToGame() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showRefineHelp(candidateCount: Long) {
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search_how_to_refine)
            .setMessage(R.string.memory_search_refine_instruction)
            .setPositiveButton(R.string.memory_search_actions) { _, _ ->
                showRefineMenu(candidateCount)
            }
            .show()
    }

    private fun promptSearchValue(size: Int, initial: Boolean) {
        if (size == 0) {
            showError(R.string.memory_search_error_generic)
            return
        }
        promptValue(
            if (initial) R.string.memory_search_initial_value else R.string.memory_search_exact_value,
            size
        ) { value ->
            if (initial) {
                runSearch { CheatEngine.startMemorySearch(size, value) }
            } else {
                runSearch { CheatEngine.refineMemorySearch(COMPARISON_EXACT, value) }
            }
        }
    }

    private fun refine(comparison: Int) {
        runSearch { CheatEngine.refineMemorySearch(comparison, 0) }
    }

    private fun runSearch(operation: () -> Long) {
        runNative(R.string.memory_search_scanning, operation) { result ->
            when {
                result >= 0 -> showRefineMenu(result)
                result == SEARCH_TOO_MANY -> showError(R.string.memory_search_error_too_many)
                result == SEARCH_INVALID_VALUE -> showError(R.string.memory_search_error_value)
                result == SEARCH_NO_GAME -> showError(R.string.memory_search_error_no_game)
                result == SEARCH_ONLINE_BLOCKED -> showError(R.string.memory_search_error_online)
                result == SEARCH_NOT_PAUSED -> showError(R.string.memory_search_error_not_paused)
                result == SEARCH_TITLE_CHANGED -> showError(R.string.memory_search_error_title_changed)
                else -> showError(R.string.memory_search_error_generic)
            }
        }
    }

    private fun showResults() {
        val values = CheatEngine.getMemorySearchResults(RESULT_LIMIT)
        if (values.isEmpty()) {
            showError(R.string.memory_search_no_results)
            return
        }
        val size = valueSize()
        val labels = Array(values.size / 2) { index ->
            val value = values[index * 2 + 1]
            context.getString(R.string.memory_search_match, index + 1, value)
        }
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search_results)
            .setItems(labels) { _, which ->
                showResultActions(which + 1, values[which * 2], values[which * 2 + 1], size)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showResultActions(match: Int, address: Long, currentValue: Long, size: Int) {
        val actions = mutableListOf<Action>()
        if (CheatEngine.canUndoMemorySearchWrite()) {
            actions += Action(R.string.memory_search_undo_write) { undoWrite() }
        } else {
            actions += Action(R.string.memory_search_test_write) {
                promptValue(R.string.memory_search_test_value, size, currentValue) { value ->
                    testWrite(address, value)
                }
            }
        }
        actions += Action(R.string.memory_search_create_cheat) {
            promptValue(R.string.memory_search_cheat_value, size, currentValue) { value ->
                promptCheatName(match, address, value, size)
            }
        }

        MaterialAlertDialogBuilder(context)
            .setTitle(context.getString(R.string.memory_search_match, match, currentValue))
            .setItems(actions.map { context.getString(it.label) }.toTypedArray()) { _, which ->
                actions[which].run()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun testWrite(address: Long, value: Long) {
        runNative(
            R.string.memory_search_writing,
            { if (CheatEngine.writeMemorySearchResult(address, value)) 1L else 0L }
        ) { result ->
            if (result == 1L) {
                MaterialAlertDialogBuilder(context)
                    .setTitle(R.string.memory_search_write_verified)
                    .setMessage(R.string.memory_search_write_verified_message)
                    .setPositiveButton(R.string.memory_search_back_to_game) { _, _ ->
                        returnToGame()
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            } else {
                showError(R.string.memory_search_write_failed)
            }
        }
    }

    private fun undoWrite() {
        runNative(R.string.memory_search_writing, { CheatEngine.undoMemorySearchWrite().toLong() }) {
            when (it) {
                1L -> showToast(R.string.memory_search_undo_success)
                0L -> showError(R.string.memory_search_undo_none)
                else -> showError(R.string.memory_search_undo_failed)
            }
        }
    }

    private fun promptCheatName(match: Int, address: Long, value: Long, size: Int) {
        val defaultName = context.getString(R.string.memory_search_default_cheat_name, match)
        val (inputLayout, input) = makeInput(R.string.cheats_name, defaultName)
        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search_create_cheat)
            .setView(inputLayout)
            .setPositiveButton(R.string.memory_search_create, null)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(DialogInterface.BUTTON_POSITIVE).setOnClickListener {
                val name = input.text?.toString()?.trim().orEmpty()
                if (name.isEmpty()) {
                    inputLayout.error = context.getString(R.string.cheats_error_no_name)
                    return@setOnClickListener
                }
                val code = memorySearchGatewayCode(address, value, size)
                check(Cheat.isValidGatewayCode(code) == 0)
                cheatsViewModel.startAddingCheat()
                cheatsViewModel.finishAddingCheat(
                    Cheat.createGatewayCode(
                        name,
                        context.getString(R.string.memory_search_cheat_notes, address),
                        code
                    )
                )
                cheatsViewModel.saveIfNeeded()
                dialog.dismiss()
                showToast(R.string.memory_search_cheat_added)
            }
        }
        dialog.show()
    }

    private fun promptValue(
        title: Int,
        size: Int,
        initialValue: Long? = null,
        onValue: (Long) -> Unit
    ) {
        val initialText = initialValue?.toString().orEmpty()
        val (inputLayout, input) = makeInput(R.string.memory_search_value_hint, initialText)
        input.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(title)
            .setMessage(R.string.memory_search_value_format)
            .setView(inputLayout)
            .setPositiveButton(android.R.string.ok, null)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(DialogInterface.BUTTON_POSITIVE).setOnClickListener {
                val value = parseMemorySearchValue(input.text?.toString().orEmpty(), size)
                if (value == null) {
                    inputLayout.error = context.getString(
                        R.string.memory_search_value_range,
                        memorySearchValueMask(size)
                    )
                    return@setOnClickListener
                }
                dialog.dismiss()
                onValue(value)
            }
        }
        dialog.show()
    }

    private fun makeInput(hint: Int, initialValue: String): Pair<TextInputLayout, TextInputEditText> {
        val input = TextInputEditText(context).apply {
            setText(initialValue)
            setSelectAllOnFocus(true)
            isSingleLine = true
        }
        val inputLayout = TextInputLayout(context).apply {
            this.hint = context.getString(hint)
            setPadding(dp(24), dp(8), dp(24), 0)
            addView(
                input,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
        }
        return inputLayout to input
    }

    private fun runNative(title: Int, operation: () -> Long, onComplete: (Long) -> Unit) {
        val progress = ProgressBar(context).apply {
            isIndeterminate = true
            setPadding(dp(32), dp(24), dp(32), dp(24))
        }
        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(title)
            .setView(progress)
            .setCancelable(false)
            .show()
        fragment.viewLifecycleOwner.lifecycleScope.launch {
            val result = withContext(Dispatchers.Default) { operation() }
            dialog.dismiss()
            onComplete(result)
        }
    }

    private fun showError(message: Int) {
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.memory_search)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    private fun showToast(message: Int) {
        Toast.makeText(context, message, Toast.LENGTH_LONG).show()
    }

    private fun valueSize(): Int = CheatEngine.getMemorySearchValueSize()

    private fun returnToGame() {
        fragment.requireActivity().onBackPressedDispatcher.onBackPressed()
    }

    private fun dp(value: Int): Int = (value * context.resources.displayMetrics.density).toInt()

    private data class Action(val label: Int, val run: () -> Unit)

    companion object {
        private const val SEARCH_NO_SESSION = -1L
        private const val SEARCH_NO_GAME = -2L
        private const val SEARCH_ONLINE_BLOCKED = -3L
        private const val SEARCH_NOT_PAUSED = -4L
        private const val SEARCH_TITLE_CHANGED = -5L
        private const val SEARCH_TOO_MANY = -6L
        private const val SEARCH_INVALID_VALUE = -7L

        private const val COMPARISON_EXACT = 0
        private const val COMPARISON_CHANGED = 1
        private const val COMPARISON_UNCHANGED = 2
        private const val COMPARISON_INCREASED = 3
        private const val COMPARISON_DECREASED = 4
        private const val DEFAULT_VALUE_SIZE = 4
        private const val RESULT_LIMIT = 50
    }
}
