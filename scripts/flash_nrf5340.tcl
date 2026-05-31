# Flash both nRF5340 cores in a single OpenOCD session.
# Recovers only if APPROTECT is engaged; otherwise uses reset halt.
# Usage: openocd -f interface/cmsis-dap.cfg -c "transport select swd" \
#               -c "adapter speed 100" -f target/nordic/nrf53.cfg \
#               -f scripts/flash_nrf5340.tcl \
#               -c "flash_both APP_HEX NET_HEX" -c shutdown

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

    puts "Resetting both cores..."
    reset run
}
