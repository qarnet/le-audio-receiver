# Flash both nRF5340 cores in a single OpenOCD session.
# Recovers only if APPROTECT is engaged; otherwise uses reset halt.
# Usage: openocd -f interface/cmsis-dap.cfg -c "transport select swd" \
#               -c "adapter speed 100" -f target/nordic/nrf53.cfg \
#               -f scripts/flash_nrf5340.tcl \
#               -c "flash_both APP_HEX NET_HEX" -c shutdown

# ── UICR APPROTECT programming ──────────────────────────────────────────────
# nRF5340 debug access is a *soft* branch: at boot, SystemInit copies
# UICR.APPROTECT into CTRLAP.APPROTECT.DISABLE. An ERASED UICR (0xFFFFFFFF)
# therefore hard-locks the debug AP at every reset even though the firmware
# runs — the chip then needs a full CTRL-AP recovery (mass erase) before it
# can be reflashed. Programming UICR.APPROTECT = Unprotected (0x50FA50FA)
# after every mass erase keeps the chip debuggable across resets.
proc _uicr_unprotect {target addr} {
    set cur 0xFFFFFFFF
    catch {set cur [$target read_memory $addr 32 1]}
    if {$cur == 0x50FA50FA} {
        return
    }
    if {$cur != 0xFFFFFFFF} {
        puts [format "WARNING: UICR @%s = 0x%08x (not erased) — leaving as-is" $addr $cur]
        return
    }
    flash fillw $addr 0x50FA50FA 1
    puts [format "UICR @%s programmed Unprotected (0x50FA50FA)" $addr]
}

# App core: APPROTECT + SECUREAPPROTECT. Call while cpuapp is halted.
proc uicr_unprotect_app {} {
    _uicr_unprotect nrf53.cpuapp 0x00FF8000
    _uicr_unprotect nrf53.cpuapp 0x00FF801C
}

# Net core: APPROTECT. Call while cpunet is halted and its bank probed.
proc uicr_unprotect_net {} {
    _uicr_unprotect nrf53.cpunet 0x01FF8000
}

# ── west flash integration ─────────────────────────────────────────────────
# check_approtect: called via --cmd-pre-load (after init, before reset halt).
# Recovers the device if APPROTECT is engaged so the subsequent reset halt works.
proc check_approtect {} {
    set locked [catch {nrf53.cpuapp arp_examine} err]
    if {$locked} {
        puts "App core locked — running nrf53_recover..."
        nrf53_recover
    }
}

# flash_west: called via --cmd-load (after init + reset halt done by west runner).
# app_hex is passed by the runner from runners.yaml config.hex_file (merged.hex).
# NET_CORE_HEX is a TCL variable set via --cmd-pre-init in CMakeLists.txt.
proc flash_west {app_hex} {
    global NET_CORE_HEX
    puts "Flashing app core: $app_hex"
    flash write_image erase $app_hex
    uicr_unprotect_app

    nrf53_cpunet_release nrf53
    catch {nrf53.cpunet arp_examine}
    targets nrf53.cpunet
    halt
    wait_halt 2000
    flash probe 2
    puts "Flashing net core: $NET_CORE_HEX"
    flash write_image erase $NET_CORE_HEX
    uicr_unprotect_net

    puts "Resetting both cores..."
    reset run
}

# ── manual flash fallback ───────────────────────────────────────────────────
proc flash_both {app_hex net_hex} {
    init

    # If app core is locked by APPROTECT, recover first.
    set app_locked [catch {nrf53.cpuapp arp_examine} err]
    if {$app_locked} {
        puts "App core locked — running nrf53_recover..."
        nrf53_recover
    }

    # Reset and halt app core cleanly (avoids "unknown state" on a running core).
    targets nrf53.cpuapp
    reset halt
    wait_halt 2000

    # Flash app core while halted at reset vector.
    puts "Flashing app core: $app_hex"
    flash write_image erase $app_hex
    uicr_unprotect_app

    # Release net core from FORCEOFF and examine.
    nrf53_cpunet_release nrf53
    catch {nrf53.cpunet arp_examine}

    # Select net core, halt, probe, then flash.
    puts "Flashing net core: $net_hex"
    targets nrf53.cpunet
    halt
    wait_halt 2000
    flash probe 2
    flash write_image erase $net_hex
    uicr_unprotect_net

    puts "Resetting both cores..."
    reset run
}
