# External PHY-action selection gap closed

## Source build chain

Selected Android source HEAD04d00c2565764c3922c9b4826b5acff1106e533f has top-level Makefile VERSION4/PATCHLEVEL19/SUBLEVEL325. It is not4.14. The saved vendor module targets4.19.191+; both select the below5.4 coant implementation, but their differing patchlevels prevent claiming a byte-identical kernel/module rebuild.

`gen4m/Android.mk:5–16` common-build6893 assigns WLAN_CHIP_ID6893, WIFI_CHIP=CONNAC2X2_SOC3_0, CONNAC_VER2_0, HIFaxi, WMTy and EMIy, then includes build_wlan_drv.mk. Its non-common branch:80ff names wlan_drv_gen4m and inherits the board chip selection. `build_wlan_drv.mk:1–20` names the proprietary .ko, requires conninfra.ko for CONNAC2_0 plus connfem.ko, and forwards MTK_COMBO_CHIP=$(WIFI_CHIP), WLAN_CHIP_ID, HIF and EMI in WIFI_OPTS.

Crucially, `gen4m/Makefile:129–148` tests findstring3_0 in MTK_COMBO_CHIP and itself sets -DSOC3_0 and CONFIG_MTK_WIFI_CONNINFRA_SUPPORT=y. Lines327–329 then set CFG_SUPPORT_CONNINFRA1 and CFG_SUPPORT_PRE_ON_PHY_ACTION1. The absence of this external variable in the built-in nord2-kout/.config is not a source selection gap. This assignment is in the driver Makefile, not an inferred Kconfig default. SOC3_0 ops install wlanPhyAction and coant/PCCIF hooks, as documented in README.

## Saved actual module evidence

Existing firmware manifest identifies already saved vendor-read/vendor.img, recorded SHA256bf8de3f4d228c105cd950ac78cc1085bb3f65f9dfc52d4e0c8db10e51780b8b5. Bounded read-only EROFS inventory inspected only /lib/modules; exact members wlan_drv_gen4m.ko, connfem.ko, modules.load and modules.dep were read to a new private module-evidence directory. No whole rootfs extraction, mount, device access or target execution occurred.

WLAN4970406 bytes SHA25607b986e247011f0efdb289a8d2859c0969e3911af30c1de656e0b4aa2696b65d. Connfem70094 bytes SHA2565d8ae602448daf9b53c43398585361f9c94e3a1cc2bb3c70e6318451318a6fdb. Modules.load lists both, and .modinfo records namewlan_drv_gen4m, vermagic4.19.191+ SMP preempt mod_unload modversions aarch64, dependenciesconninfra,wmt_chrdev_wifi,connfem and srcversion533BB7E5866E52F63B9ACCB. This pins the saved module; no claim is made that it is currently loaded.

LLVM ELF symbols show wlanPhyAction0x15c098, wlanSendPhyAction0x15bb60, wlanGetEpaElnaFromNvram0x15b758, wlanPreCalPwrOn0x15c0d8, wlanPreCal0x15cae4, wlanDownloadFW0x15f9e8 and gEmiCalResult/Offset/Size. The unstripped module also identifies SOC3_0 through mt66xx_chip_info_soc3_0 and soc3_0_fw_dl_ops.

Relocation .data0x907d0 points to wlanPhyAction, offset0x38 inside soc3_0_fw_dl_ops0x90798. wlanDownloadFW disassembly15fb74 loads fw_dl_ops+56, calls it15fb80 and tests returned status15fb88 before coant/N9 continuation. This closes the stronger question of whether the feature is merely dead source: it is compiled and installed in the saved SOC3_0 module. Chipinfo data relocations90ae0/90ae8 wire PCCIFon/off and90b00/90b08 wire coant enable/disable. The focused disassembly also shows wlanPhyAction calling wlanAccessCalibrationEMI with NULL/FALSE, then choosing cmd2 iff result0, else1, and calling wlanSendPhyAction with CAL tag1.

PCCIFon162e6c constructs10003314, calls write0 thenwrite1 and returns0. Coant162fcc reads byte243 and branches away if0; later call is connectivity_export_pmic_ldo_vfe28_lp. Saved actual Connfem getter disassembly zeroes outputs before null-context branch returning-95, matching source absent-context policy. No actual Connfem probe/runtime behavior was observed.

Evidence files: module-evidence/{identity.json,symbols-focused.txt,relocations-focused.txt,modinfo.txt,wlan-focused.S,connfem-getters.S,modules.load,modules.dep}. Disassembly is restricted to named functions, with only .rela.text relocations inside their symbol ranges appended. Global relocation evidence is filtered to the relevant feature names/hook addresses. REA MCP tools were absent and no rea CLI was in PATH; previously documented unavailable-provider fallback was retained. Ordinary LLVM/EROFS outputs are not presented as REA Evidence, and no providers/software were installed.

## Conclusions and limits

Source selection and actual saved module wiring both establish the PHY-action/precal and coexistence path. The previous actual-external-flag uncertainty is closed. Exact Android product build invocation, compiler version and bit-for-bit source/binary equivalence remain unproven and are unnecessary for this narrower feature-selection conclusion. These static facts do not establish that Android ran calibration, a runtime backup exists, RF action is authorized, PCCIF modem side effects are harmless, or native firmware startup is ready. Preserve those execution prerequisites from README.
