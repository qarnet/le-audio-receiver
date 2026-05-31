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
