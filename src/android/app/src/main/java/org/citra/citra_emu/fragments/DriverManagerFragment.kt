// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import androidx.navigation.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.R as MaterialR
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textview.MaterialTextView
import com.google.android.material.transition.MaterialSharedAxis
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.R
import org.citra.citra_emu.adapters.DriverAdapter
import org.citra.citra_emu.databinding.FragmentDriverManagerBinding
import org.citra.citra_emu.utils.FileUtil.inputStream
import org.citra.citra_emu.utils.GpuDriverHelper
import org.citra.citra_emu.viewmodel.DriverViewModel
import org.citra.citra_emu.viewmodel.HomeViewModel

class DriverManagerFragment : Fragment() {
    private var _binding: FragmentDriverManagerBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentDriverManagerBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setNavigationVisibility(visible = false, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = false)

        if (!driverViewModel.isInteractionAllowed) {
            DriversLoadingDialogFragment().show(
                childFragmentManager,
                DriversLoadingDialogFragment.TAG
            )
        }

        binding.toolbarDrivers.setNavigationOnClickListener {
            binding.root.findNavController().popBackStack()
        }

        binding.buttonInstallGuide.setOnClickListener {
            showRecommendedDriverGuide()
        }

        binding.buttonInstall.setOnClickListener {
            getDriver.launch(arrayOf("application/zip"))
        }

        binding.listDrivers.apply {
            layoutManager = GridLayoutManager(
                requireContext(),
                resources.getInteger(R.integer.game_grid_columns)
            )
            adapter = DriverAdapter(driverViewModel)
        }

        viewLifecycleOwner.lifecycleScope.apply {
            launch {
                driverViewModel.driverList.collectLatest {
                    (binding.listDrivers.adapter as DriverAdapter).submitList(it)
                }
            }
            launch {
                driverViewModel.newDriverInstalled.collect {
                    if (_binding != null && it) {
                        (binding.listDrivers.adapter as DriverAdapter).apply {
                            notifyItemChanged(driverViewModel.previouslySelectedDriver)
                            notifyItemChanged(driverViewModel.selectedDriver)
                            driverViewModel.setNewDriverInstalled(false)
                        }
                    }
                }
            }
        }

        setInsets()
    }

    // Start installing requested driver
    override fun onStop() {
        super.onStop()
        driverViewModel.onCloseDriverManager()
    }

    private fun setInsets() = ViewCompat.setOnApplyWindowInsetsListener(
        binding.root
    ) { _: View, windowInsets: WindowInsetsCompat ->
        val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
        val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

        val leftInsets = barInsets.left + cutoutInsets.left
        val rightInsets = barInsets.right + cutoutInsets.right

        val mlpAppBar = binding.toolbarDrivers.layoutParams as ViewGroup.MarginLayoutParams
        mlpAppBar.leftMargin = leftInsets
        mlpAppBar.rightMargin = rightInsets
        binding.toolbarDrivers.layoutParams = mlpAppBar

        val mlplistDrivers = binding.listDrivers.layoutParams as ViewGroup.MarginLayoutParams
        mlplistDrivers.leftMargin = leftInsets
        mlplistDrivers.rightMargin = rightInsets
        binding.listDrivers.layoutParams = mlplistDrivers

        val fabSpacing = resources.getDimensionPixelSize(R.dimen.spacing_fab)
        val mlpTurnipFab =
            binding.buttonInstallGuide.layoutParams as ViewGroup.MarginLayoutParams
        mlpTurnipFab.leftMargin = leftInsets + fabSpacing
        mlpTurnipFab.rightMargin = resources.getDimensionPixelSize(R.dimen.spacing_med)
        mlpTurnipFab.bottomMargin = barInsets.bottom + fabSpacing
        binding.buttonInstallGuide.layoutParams = mlpTurnipFab

        val mlpFab =
            binding.buttonInstall.layoutParams as ViewGroup.MarginLayoutParams
        mlpFab.leftMargin = resources.getDimensionPixelSize(R.dimen.spacing_med)
        mlpFab.rightMargin = rightInsets + fabSpacing
        mlpFab.bottomMargin = barInsets.bottom + fabSpacing
        binding.buttonInstall.layoutParams = mlpFab

        binding.listDrivers.updatePadding(
            bottom = barInsets.bottom +
                resources.getDimensionPixelSize(R.dimen.spacing_bottom_list_fab)
        )

        windowInsets
    }

    private fun showRecommendedDriverGuide() {
        binding.buttonInstallGuide.isEnabled = false
        viewLifecycleOwner.lifecycleScope.launch {
            val options = withContext(Dispatchers.IO) {
                GpuDriverHelper.getRecommendedDriverOptions()
            }
            if (_binding == null) {
                return@launch
            }
            binding.buttonInstallGuide.isEnabled = true

            if (options.isEmpty()) {
                IndeterminateProgressDialogFragment.newInstance(
                    requireActivity(),
                    R.string.installing_driver,
                    false
                ) {
                    val driverPackage = GpuDriverHelper.downloadRecommendedTurnipDriver()
                        ?: return@newInstance getString(R.string.turnip_driver_install_error)
                    return@newInstance installDriverPackage(driverPackage)
                }.show(childFragmentManager, IndeterminateProgressDialogFragment.TAG)
                return@launch
            }

            showRecommendedDriverDialog(options)
        }
    }

    private fun showRecommendedDriverDialog(
        options: List<GpuDriverHelper.RecommendedDriverOption>
    ) {
        val context = requireContext()
        val spacingSmall = resources.getDimensionPixelSize(R.dimen.spacing_small)
        val spacingMed = resources.getDimensionPixelSize(R.dimen.spacing_med)
        val spacingLarge = resources.getDimensionPixelSize(R.dimen.spacing_large)
        val content = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(spacingLarge, spacingMed, spacingLarge, spacingSmall)
        }
        val scrollView = ScrollView(context).apply {
            addView(
                content,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
            )
        }

        lateinit var dialog: AlertDialog
        options.forEach { option ->
            val card = createDriverOptionCard(option) {
                dialog.dismiss()
                installDriverOption(option)
            }
            content.addView(
                card,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply {
                    bottomMargin = spacingMed
                }
            )
        }

        dialog = MaterialAlertDialogBuilder(context)
            .setTitle(R.string.recommended_gpu_driver_title)
            .setView(scrollView)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        dialog.show()
    }

    private fun createDriverOptionCard(
        option: GpuDriverHelper.RecommendedDriverOption,
        onInstall: () -> Unit
    ): MaterialCardView {
        val context = requireContext()
        val spacingSmall = resources.getDimensionPixelSize(R.dimen.spacing_small)
        val spacingMed = resources.getDimensionPixelSize(R.dimen.spacing_med)
        val spacingLarge = resources.getDimensionPixelSize(R.dimen.spacing_large)
        val card = MaterialCardView(
            context,
            null,
            MaterialR.attr.materialCardViewOutlinedStyle
        ).apply {
            isClickable = true
            isFocusable = true
            radius = spacingMed.toFloat()
            setOnClickListener { onInstall() }
        }
        val content = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(spacingLarge, spacingLarge, spacingLarge, spacingLarge)
        }
        val title = MaterialTextView(context).apply {
            text = option.title
            setTextAppearance(MaterialR.style.TextAppearance_Material3_TitleMedium)
        }
        val note = MaterialTextView(context).apply {
            text = option.note
            setTextAppearance(MaterialR.style.TextAppearance_Material3_BodyMedium)
        }
        val asset = MaterialTextView(context).apply {
            text = getString(R.string.driver_asset_label, option.assetName)
            setTextAppearance(MaterialR.style.TextAppearance_Material3_BodySmall)
        }
        val installButton = MaterialButton(context).apply {
            text = getString(R.string.download_and_install_driver)
            icon = resources.getDrawable(R.drawable.ic_install_driver, context.theme)
            iconGravity = MaterialButton.ICON_GRAVITY_TEXT_START
            gravity = Gravity.CENTER
            isAllCaps = false
            setOnClickListener { onInstall() }
        }

        content.addView(
            title,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        )
        content.addView(
            note,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = spacingSmall
            }
        )
        content.addView(
            asset,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = spacingSmall
            }
        )
        content.addView(
            installButton,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = spacingMed
            }
        )
        card.addView(content)
        return card
    }

    private fun installDriverOption(option: GpuDriverHelper.RecommendedDriverOption) {
        IndeterminateProgressDialogFragment.newInstance(
            requireActivity(),
            R.string.installing_driver,
            false
        ) {
            val driverPackage = GpuDriverHelper.downloadDriverOption(option)
                ?: return@newInstance getString(R.string.turnip_driver_install_error)

            return@newInstance installDriverPackage(driverPackage)
        }.show(childFragmentManager, IndeterminateProgressDialogFragment.TAG)
    }

    private fun installDriverPackage(driverPackage: GpuDriverHelper.DriverPackage): Any {
        val driverInList = driverViewModel.driverList.value.firstOrNull {
            it.second == driverPackage.metadata
        }
        val driverUri = if (driverInList != null) {
            val driverIndex = driverViewModel.driverList.value.indexOf(driverInList)
            driverViewModel.setSelectedDriverIndex(driverIndex)
            driverInList.first
        } else {
            val driverData = Pair(driverPackage.file.uri, driverPackage.metadata)
            driverViewModel.addDriver(driverData)
            driverPackage.file.uri
        }

        if (!GpuDriverHelper.installCustomDriverPartial(driverUri)) {
            return getString(R.string.select_gpu_driver_error)
        }

        driverViewModel.setDriverReady()
        driverViewModel.setNewDriverInstalled(true)
        val installedDriverName = GpuDriverHelper.customDriverData.name
            ?: driverPackage.metadata.name
            ?: getString(R.string.system_gpu_driver)
        return getString(R.string.select_gpu_driver_install_success, installedDriverName)
    }

    private val getDriver =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { result ->
            if (result == null) {
                return@registerForActivityResult
            }

            IndeterminateProgressDialogFragment.newInstance(
                requireActivity(),
                R.string.installing_driver,
                false
            ) {
                // Ignore file exceptions when a user selects an invalid zip
                val driverFile: DocumentFile
                try {
                    driverFile = GpuDriverHelper.copyDriverToExternalStorage(result)
                        ?: throw IOException("Driver failed validation!")
                } catch (_: IOException) {
                    return@newInstance getString(R.string.select_gpu_driver_error)
                }

                val driverData = GpuDriverHelper.getMetadataFromZip(driverFile.inputStream())
                val driverInList =
                    driverViewModel.driverList.value.firstOrNull { it.second == driverData }
                if (driverInList != null) {
                    driverFile.delete()
                    val driverIndex = driverViewModel.driverList.value.indexOf(driverInList)
                    driverViewModel.setSelectedDriverIndex(driverIndex)
                    return@newInstance installDriverPackage(
                        GpuDriverHelper.DriverPackage(driverFile, driverData)
                    )
                } else {
                    return@newInstance installDriverPackage(
                        GpuDriverHelper.DriverPackage(driverFile, driverData)
                    )
                }
            }.show(childFragmentManager, IndeterminateProgressDialogFragment.TAG)
        }
}
