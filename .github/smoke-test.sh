#!/usr/bin/env bash
# Headless smoke test for the packaged engine. Launches the binary with SDL's
# dummy video/audio drivers (so it needs no display), confirms it starts and
# loads the default map — which means its bundled SDL2 and its assets all
# resolved — then stops it. Used by the release workflow so a dead-on-arrival
# binary never ships. Usage: smoke-test.sh <dist-dir>
set -u
dir="$1"
cd "$dir"

bin=./portal_engine
[ -f ./portal_engine.exe ] && bin=./portal_engine.exe

export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
"$bin" > smoke.log 2>&1 &
pid=$!
sleep 3
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

echo "----- engine output -----"
cat smoke.log
echo "-------------------------"
if grep -q "loaded 'map.txt'" smoke.log; then
    echo "smoke test: OK (engine started and loaded the map)"
else
    echo "smoke test: FAILED (engine did not start / load map.txt)" >&2
    exit 1
fi
