# ── nRF5340: SW Split hci_ipc net core ────────────────────────
if(SB_CONFIG_NETCORE_HCI_IPC)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/hci_ipc)

  # Devicetree: enable bt_hci_controller, disable bt_hci_sdc,
  # and point zephyr,bt-hci to the SW Split node.
  add_overlay_dts(
    hci_ipc
    ${ZEPHYR_BASE}/snippets/bt-ll-sw-split/bt-ll-sw-split.overlay
  )

  # Kconfig: SW Split LL, peripheral-only (sink-only unicast receiver, fits 64 KB net core)
  add_overlay_config(
    hci_ipc
    ${NET_APP_SRC_DIR}/nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf
  )

  # Exclude hci_ipc from west flash domain sequence: the le-audio-receiver app domain
  # runner (flash_west proc) flashes both cores in a single OpenOCD session.
  set_target_properties(hci_ipc PROPERTIES BUILD_ONLY TRUE)
endif()

# ── nRF54L15: FLPR (RISC-V VPR) co-processor ───────────────────
# Only build the FLPR image when the cpuapp target is nRF54L15.
# The FLPR image executes from SRAM (loaded from RRAM by the VPR
# launcher driver). Application source lives in src/flpr/.
# Kconfig symbol BOARD_NRF54L15DK_NRF54L15_CPUAPP is set by sysbuild
# when the board qualifier matches.
if(SB_CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUAPP OR
   CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUAPP OR
   BOARD MATCHES ".*nrf54l15.*cpuapp")
  message(STATUS "FLPR: detected nRF54L15 cpuapp (BOARD=${BOARD}), adding FLPR domain")

  set(FLPR_BOARD "nrf54l15dk/nrf54l15/cpuflpr")

  ExternalZephyrProject_Add(
    APPLICATION flpr
    SOURCE_DIR ${APP_DIR}/src/flpr
    BOARD ${FLPR_BOARD}
    BOARD_REVISION ${BOARD_REVISION}
  )

  # Build flpr before cpuapp so the VPR launcher can reference it.
  sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} flpr)

  # nordic-flpr snippet: enables &cpuflpr_vpr on cpuapp, which triggers
  # the VPR launcher driver to copy the FLPR image from source memory
  # (cpuflpr_rram) to execution memory (cpuflpr_sram) and release from reset.
  # The snippet's default cpuflpr_sram at 0x20028000 conflicts with our
  # shared IPC region; the overlay below overrides to 0x20030000.
  sysbuild_cache_set(VAR ${DEFAULT_IMAGE}_SNIPPET APPEND REMOVE_DUPLICATES nordic-flpr)

  # Override snippet's memory layout: FLPR execution SRAM at 0x20030000 (64 KB),
  # keeping cpuapp SRAM at 0x20000000..0x20028000 (160 KB) and shared IPC at
  # 0x20028000..0x20030000 (32 KB).
  add_overlay_dts(
    ${DEFAULT_IMAGE}
    ${APP_DIR}/boards/nrf54l15dk_nrf54l15_cpuapp_vpr_memory.overlay
  )
endif()
