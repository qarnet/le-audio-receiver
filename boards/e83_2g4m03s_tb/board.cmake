# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_E83_2G4M03S_TB_NRF5340_CPUAPP_NS)
  set(TFM_PUBLIC_KEY_FORMAT "full")
endif()

if(CONFIG_BOARD_E83_2G4M03S_TB_NRF5340_CPUAPP OR CONFIG_BOARD_E83_2G4M03S_TB_NRF5340_CPUAPP_NS)
  board_runner_args(pyocd "--target=nrf5340_xxaa_app")
endif()

if(CONFIG_TFM_FLASH_MERGED_BINARY)
  set_property(TARGET runners_yaml_props_target PROPERTY hex_file tfm_merged.hex)
endif()

if(CONFIG_BOARD_E83_2G4M03S_TB_NRF5340_CPUNET)
  board_runner_args(pyocd "--target=nrf5340_xxaa_net")
endif()

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
