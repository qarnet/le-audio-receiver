# SPDX-License-Identifier: Apache-2.0
#
# Sysbuild: mirror the upstream BAP unicast client sample, add hci_ipc on
# the nRF5340 net core, and configure the net core for the SoftDevice
# Controller (SDC) as central ISO controller: the sample base prj.conf
# (controller-agnostic) plus the repository SDC overlay. No snippet is
# applied; the nrf5340_cpunet DT default selects the SDC node
# (bt_hci_sdc okay, zephyr,bt-hci chosen). The SW-split wiring this
# replaces is recorded in docs/development/
# system-hil-rh3-modea10-sdc-handoff.md.

if(SB_CONFIG_NET_CORE_IMAGE_HCI_IPC)
  set(NET_APP hci_ipc)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/${NET_APP})

  ExternalZephyrProject_Add(
    APPLICATION ${NET_APP}
    SOURCE_DIR  ${NET_APP_SRC_DIR}
    BOARD       ${SB_CONFIG_NET_CORE_BOARD}
  )

  # Base configuration is the sample's own prj.conf (BT_HCI_RAW over IPC
  # with generic buffer defaults); the repository SDC overlay below adds
  # the central ISO roles, SDC symbols, and fixture tightening.

  # Repository overlay: SDC central ISO configuration for one exact peer.
  add_overlay_config(
    hci_ipc
    ${CMAKE_CURRENT_LIST_DIR}/overlay-nrf5340_cpunet_sdc.conf
  )

  list(APPEND ${NET_APP}_SNIPPET ${SNIPPET})
  set(${NET_APP}_SNIPPET ${${NET_APP}_SNIPPET} CACHE STRING "" FORCE)

  native_simulator_set_child_images(${DEFAULT_IMAGE} ${NET_APP})

  # Merged hex artifacts with the nRF5340 dual-domain convention:
  # build/hil-source/merged.hex = app core image,
  # build/hil-source/merged_CPUNET.hex = net core image.  (The historical
  # NCS mechanism that produced these names was removed upstream; the
  # mergehex.py invocation below reproduces the exact outputs.)
  foreach(pair "app;merged.hex" "hci_ipc;merged_CPUNET.hex")
    list(GET pair 0 merge_image)
    list(GET pair 1 merge_output)
    set(merge_input ${CMAKE_BINARY_DIR}/${merge_image}/zephyr/zephyr.hex)
    set(merge_target merged_hex_target_${merge_image})
    add_custom_command(
      OUTPUT ${CMAKE_BINARY_DIR}/${merge_output}
      COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/mergehex.py
              -o ${CMAKE_BINARY_DIR}/${merge_output}
              --overlap=replace ${merge_input}
      DEPENDS ${merge_image}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating ${merge_output}"
    )
    add_custom_target(${merge_target} ALL DEPENDS ${CMAKE_BINARY_DIR}/${merge_output})
  endforeach()
endif()
