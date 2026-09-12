# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Expose project's boards/ dir to Zephyr for custom board discovery.
list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Probe selection happens at flash time (fw-flash-5340 → nrf-probes), not at
# configure time — a serial baked into runners.yaml goes stale when probes
# are replugged.

# Net core hex path: sysbuild top-level merged_CPUNET.hex.
# CMAKE_BINARY_DIR at this point is the app domain build dir
# (build/<target>/le-audio-receiver), so ../merged_CPUNET.hex resolves to
# build/<target>/merged_CPUNET.hex.
get_filename_component(_NET_CORE_HEX "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver)

target_sources(app PRIVATE
  src/main.c
  src/app_lifecycle.c
  src/bt_bap.c
  src/bt_pairing_policy.c
  src/audio_decode.c
  src/audio_stream_session.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_rate_convert.c
  src/audio_modea.c
  src/audio_iso_seq.c
  src/audio_stats.c
  src/audio_volume.c
  src/audio_timing_math.c
  src/stream_lifecycle.c
  src/audio_offload.c
)

zephyr_sources_ifdef(CONFIG_AUDIO_PERF_MEASUREMENT src/audio_perf.c)

zephyr_sources_ifdef(CONFIG_HIL_HCI_REMOVE_ISO_PATH_TRACE src/hci_remove_iso_path_trace.c)

if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE)
  target_sources(app PRIVATE src/sdc_hci_remove_iso_path_trace.c)
  target_link_options(app INTERFACE
    -Wl,--wrap=sdc_hci_cmd_le_remove_iso_data_path
    -Wl,--wrap=hci_internal_msg_get
    -Wl,--wrap=hci_internal_cmd_put
    -Wl,--wrap=multithreading_lock_release
    -Wl,--wrap=k_work_submit_to_queue
  )
  if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK)
    target_link_options(app INTERFACE
      -Wl,--wrap=k_sched_unlock
      -Wl,--wrap=z_impl_k_yield
    )
  endif()
  if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION)
    target_link_options(app INTERFACE
      -Wl,--wrap=bt_buf_get_evt
      -Wl,--wrap=bt_buf_get_rx
    )
  endif()
  if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME)
    target_link_options(app INTERFACE
      -Wl,--wrap=net_buf_unref
    )
    if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION)
      target_link_options(app INTERFACE
        -Wl,--wrap=bt_conn_recv
      )
      if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH)
        target_link_options(app INTERFACE
          -Wl,--wrap=k_work_flush
        )
        if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE)
          target_link_options(app INTERFACE
            -Wl,--wrap=z_impl_k_sem_take
          )
          if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND)
            target_link_options(app INTERFACE
              -Wl,--wrap=z_pend_curr
            )
            if(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE)
              target_link_options(app INTERFACE
                -Wl,--wrap=z_impl_k_sem_give
              )
            endif()
          endif()
        endif()
      endif()
    endif()
  endif()
endif()

zephyr_sources_ifdef(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR src/audio_asrc.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_APLL src/audio_clock_actuator_apll.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_NONE src/audio_clock_actuator_none.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/audio_timing_nrf54.c)
zephyr_sources_ifndef(CONFIG_SOC_NRF54L15 src/audio_timing_none.c)
zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)
zephyr_sources_ifdef(CONFIG_SHELL src/bt_shell.c)
zephyr_sources_ifdef(CONFIG_USER_PAIRING_CONTROL src/pairing_mode.c)
zephyr_sources_ifdef(CONFIG_USER_PAIRING_CONTROL src/bt_bap_pairing_adapter.c)
zephyr_sources_ifdef(CONFIG_USER_PAIRING_INPUT src/user_pairing_io.c)
# flpr_shell.c needs both SHELL and the nRF54L15 FLPR stack;
# zephyr_sources_ifdef accepts a single condition, so use an explicit
# if() with target_sources for the two-condition gate.
if(CONFIG_SHELL AND CONFIG_SOC_NRF54L15)
  target_sources(app PRIVATE src/flpr_shell.c)
endif()
zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS src/flpr_acceptance_shell.c)
# FLPR acceptance orchestration (ring test, stalls, stale produce,
# stress, fault hang, gates 1-6) — compiled only with the acceptance
# diagnostics config.  The shared control-ACK engine is production-neutral
# (used by the core coordinated reset) and compiles on every nRF54L15
# build, exactly like flpr_ring_mgr.c.
zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS src/flpr_acceptance.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_control_ack.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_handshake.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_ring.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_cache.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_ring_mgr.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_runtime.c)

# LSP compile-database link (R8/R9 clangd work): expose this image's
# compile_commands.json at the repo root for clangd's closest-ancestor
# discovery.  Guarded on CONFIG_SOC_NRF54L15 so only the production
# receiver target (nRF54L15) owns the root link — building nRF5340
# must not flip the root flavor.  file(CREATE_LINK) is idempotent; a
# reconfigure (--pristine included) re-creates it.  Other images create
# their own links in their app CMakeLists; BSim and tests/unit links
# are maintained by scripts/gen-lsp-links.sh (builds live outside the
# repo or in ephemeral dirs).
if(CONFIG_SOC_NRF54L15)
  file(CREATE_LINK
       "${CMAKE_BINARY_DIR}/compile_commands.json"
       "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
       SYMBOLIC)
endif()

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
