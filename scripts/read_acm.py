#!/usr/bin/env python3
"""Robust ACM reader: reopens port if device resets."""
import serial, time, os, sys
PORT = sys.argv[1]
OUT  = sys.argv[2]
DURATION = int(sys.argv[3]) if len(sys.argv) > 3 else 30
DEV = f"/dev/{PORT}"
T_START = time.time()
print(f"[{PORT}] Starting reader (duration={DURATION}s)...", flush=True)

# Wait for port to appear (reconnect after reset may take a moment)
for _ in range(30):
    if os.access(DEV, os.R_OK):
        break
    time.sleep(0.1)
else:
    print(f"[{PORT}] Port never appeared!", flush=True)
    sys.exit(1)

with open(OUT, "wb") as f:
    while time.time() - T_START < DURATION:
        try:
            s = serial.Serial(DEV, 115200, timeout=0.5)
            print(f"[{PORT}] Opened {DEV}", flush=True)
            while time.time() - T_START < DURATION:
                data = s.read(1024)
                if data:
                    f.write(data)
                    f.flush()
                else:
                    # Check if port is still there
                    if not os.access(DEV, os.R_OK):
                        print(f"[{PORT}] Port disappeared, closing", flush=True)
                        break
            s.close()
        except serial.SerialException as e:
            print(f"[{PORT}] SerialException: {e}", flush=True)
            time.sleep(0.2)
        except Exception as e:
            print(f"[{PORT}] Error: {e}", flush=True)
            time.sleep(0.2)
    
print(f"[{PORT}] Done ({int(time.time()-T_START)}s)", flush=True)
