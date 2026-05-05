#!/bin/bash
# Monitor both nRF5340 cores simultaneously with flag-file interrupt + timeout.
#   touch /tmp/nrf_monitor_stop   — stops monitoring cleanly
#   ./scripts/monitor.sh [seconds] — default 120

TIMEOUT="${1:-120}"
FLAG="/tmp/nrf_monitor_stop"
APP_DEV="${2:-/dev/ttyACM0}"
NET_DEV="${3:-/dev/ttyACM1}"

rm -f "$FLAG"

cleanup() {
    kill 0
    rm -f "$FLAG"
    exit 0
}
trap cleanup INT TERM

watch_device() {
    local dev="$1"
    local tag="$2"
    stty -F "$dev" 115200 raw -echo 2>/dev/null || {
        echo "[$tag] Can't open $dev"
        return 1
    }
    while true; do
        if [ -f "$FLAG" ]; then
            exit 0
        fi
        if read -r -t 0.5 line < "$dev"; then
            echo "[$tag] $line"
        fi
    done
}

echo "Monitoring APP=$APP_DEV (+ NET=$NET_DEV) for ${TIMEOUT}s."
echo "  touch $FLAG  to stop."

watch_device "$APP_DEV" "APP" &
PID_APP=$!
watch_device "$NET_DEV" "NET" &
PID_NET=$!

END=$((SECONDS + TIMEOUT))
while [ $SECONDS -lt $END ]; do
    if [ -f "$FLAG" ]; then
        break
    fi
    sleep 0.5
done

touch "$FLAG"
wait $PID_APP $PID_NET 2>/dev/null
echo "Stopped."
