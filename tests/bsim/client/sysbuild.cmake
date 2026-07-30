# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_NET_CORE_IMAGE_HCI_IPC)
  set(NET_APP hci_ipc)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/${NET_APP})

  ExternalZephyrProject_Add(
    APPLICATION ${NET_APP}
    SOURCE_DIR  ${NET_APP_SRC_DIR}
    BOARD       ${SB_CONFIG_NET_CORE_BOARD}
  )

  # Main netcore conf: central+peripheral ISO SW Split LL
  set(${NET_APP}_CONF_FILE
    ${NET_APP_SRC_DIR}/nrf5340_cpunet_iso-bt_ll_sw_split.conf
    CACHE INTERNAL ""
  )

  # Extra controller config: ISO TX for source ASE
  set(${NET_APP}_EXTRA_CONF_FILE
    ${APP_DIR}/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf
    CACHE INTERNAL ""
  )

  native_simulator_set_child_images(${DEFAULT_IMAGE} ${NET_APP})
endif()

native_simulator_set_final_executable(${DEFAULT_IMAGE})

native_simulator_set_primary_mcu_index(${DEFAULT_IMAGE})
