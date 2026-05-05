if(SB_CONFIG_NETCORE_HCI_IPC)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/hci_ipc)

  add_overlay_config(
    hci_ipc
    ${NET_APP_SRC_DIR}/nrf5340_cpunet_iso-bt_ll_sw_split.conf
  )
endif()
