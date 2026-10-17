#!/bin/bash
# Injects libDota2Helper.so into a running Dota 2 without sudo.
# Requires kernel.yama.ptrace_scope = 0 (see README / watch.sh).
set -u

here="$(cd "$(dirname "$0")" && pwd)"
filename="libDota2Helper.so"
dota_dir="${DOTA_DIR:-$HOME/.local/share/Steam/steamapps/common/dota 2 beta/game/bin/linuxsteamrt64}"
inject_path="$dota_dir/$filename"
delay="${D2H_DELAY:-5}"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

dota_pid=$(pidof dota2)
if [ -z "$dota_pid" ]; then
    log "Dota 2 is not running"
    exit 1
fi

if grep -q "$filename" "/proc/$dota_pid/maps"; then
    log "already injected into $dota_pid"
    exit 0
fi

# pressure-vessel can't see paths outside the game dir, so the game loads its own copy
cp "$here/$filename" "$inject_path" || exit 1

# wait until the game has its client and UI libraries mapped, then give it a few more seconds
for _ in $(seq 1 180); do
    if grep -q "libclient.so" "/proc/$dota_pid/maps" 2>/dev/null && grep -q "libpanorama.so" "/proc/$dota_pid/maps" 2>/dev/null; then
        break
    fi
    sleep 1
    kill -0 "$dota_pid" 2>/dev/null || { log "Dota 2 exited while loading"; exit 1; }
done
sleep "$delay"

# keep Steam's crash reporter from uploading dumps that include us
mkdir -p /tmp/dumps 2>/dev/null && chmod 000 /tmp/dumps 2>/dev/null

killall -STOP steam steamwebhelper 2>/dev/null

output="$(gdb -n -q -batch \
  -ex "attach $dota_pid" \
  -ex "set \$dlopen = (void*(*)(char*, int)) dlopen" \
  -ex "call \$dlopen(\"$inject_path\", 2)" \
  -ex "call (char*) dlerror()" \
  -ex "detach" \
  -ex "quit" 2>&1)"

sleep 0.2
killall -CONT steamwebhelper steam 2>/dev/null

if echo "$output" | grep -q '= (void \*) 0x0'; then
    log "injection FAILED: $(echo "$output" | grep -i 'error\|\$2' | tail -2)"
    exit 1
elif grep -q "$filename" "/proc/$dota_pid/maps"; then
    log "injected into $dota_pid"
    exit 0
else
    log "could not confirm injection:"
    echo "$output" | tail -5
    exit 1
fi
