#!/usr/bin/env bash
#
# Launch the Rust demo, record which GL stack the host toolkit actually pulled
# in, and report whether the process survived.
#
# The GL stack matters for interpreting the result. MapLibre's EGL headless
# backend only collides with the host toolkit when the toolkit is also on EGL;
# if glutin falls back to GLX on this machine, the run proves nothing about the
# reported failure regardless of whether it passes.
#
# Exit codes: 0 survived, 3 exited on its own, 4 never started.

set -u

APP=${APP:-./target/debug/maplibre_native_slint}
LOG=${LOG:-gui.log}
LIFETIME=${LIFETIME:-25}

if [ ! -x "$APP" ]; then
    echo "no such executable: $APP"
    exit 4
fi

"$APP" > "$LOG" 2>&1 &
pid=$!

# Sample the loaded objects once the toolkit has had time to bring up a window.
mapped=""
for _ in $(seq 1 15); do
    kill -0 "$pid" 2>/dev/null || break
    if [ -r "/proc/$pid/maps" ]; then
        mapped=$(grep -oE 'lib(EGL|GLX|GL)\.so[^ ]*' "/proc/$pid/maps" | sort -u | tr '\n' ' ')
        case "$mapped" in
            *libEGL*|*libGLX*) break ;;
        esac
    fi
    sleep 1
done
echo "host GL objects mapped: ${mapped:-none}"

elapsed=0
while kill -0 "$pid" 2>/dev/null && [ "$elapsed" -lt "$LIFETIME" ]; do
    sleep 1
    elapsed=$((elapsed + 1))
done

if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    echo "survived ${LIFETIME}s"
    exit 0
fi

wait "$pid"
code=$?
echo "exited on its own after ${elapsed}s with code $code"
exit 3
