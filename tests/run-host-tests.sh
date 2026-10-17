#!/usr/bin/env bash
# Builds the service for the host with fake ICC and LED functions and checks the API with curl: the limits,
# refusing a request while a sequence plays, errors, and putting the LED back. Needs a C compiler and curl.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'kill $server 2>/dev/null || true; rm -rf "$work"' EXIT
sources=("$root/tests/host_stubs.c" "$root/src/config.c" "$root/src/patterns.c" "$root/src/player.c"
    "$root/src/http.c" "$root/src/json.c" "$root/src/temperature.c")
config="$work/config"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -I "$root/tests/include" -DSIGNAL_DEV -DCONFIG_DIRECTORY="\"$config\"" \
    -o "$work/signal" "$root/tests/host_main.c" "${sources[@]}" -lpthread
"$work/signal" 2>"$work/log" &
server=$!
sleep 0.3
url=http://127.0.0.1:9150
failures=0
check() { # check <name> <expected status> <curl arguments...>
    local name=$1 expected=$2; shift 2
    local status
    status=$(curl -s -o "$work/body" -w '%{http_code}' "$@")
    if [[ $status == "$expected" ]]; then echo "ok   $name ($status)"
    else echo "FAIL $name: $status, expected $expected: $(cat "$work/body")"; failures=$((failures + 1)); fi
}
beep() { check "$1" "$2" -X POST "$url/beep" -d "$3"; }
led() { check "$1" "$2" -X POST "$url/led" -d "$3"; }

check "status" 200 "$url/status"
beep "6 beeps" 202 '{"pattern":[{"tone":1,"pause_ms":100},{"tone":1,"pause_ms":100},{"tone":1,"pause_ms":100},{"tone":1,"pause_ms":100},{"tone":1,"pause_ms":100},{"tone":1}]}'
beep "while a beep sequence plays" 409 '{"pattern":[{"tone":1}]}'
sleep 0.8
beep "25 beeps" 400 "{\"pattern\":[$(printf '{"tone":1},%.0s' {1..24}){\"tone\":1}]}"
beep "pauses over 10 s" 400 "{\"pattern\":[$(printf '{"tone":1,"pause_ms":2000},%.0s' {1..5}){\"tone\":1,\"pause_ms\":100},{\"tone\":1}]}"
beep "pause 99 ms" 400 '{"pattern":[{"tone":1,"pause_ms":99},{"tone":1}]}'
beep "pause 2001 ms" 400 '{"pattern":[{"tone":1,"pause_ms":2001},{"tone":1}]}'
beep "tone 4" 400 '{"pattern":[{"tone":4}]}'
beep "no pattern" 400 '{}'
beep "not JSON" 400 'beep'
beep "pause 2000 ms" 202 '{"pattern":[{"tone":2,"pause_ms":2000},{"tone":3}]}'
check "stop" 200 -X POST "$url/stop"
sleep 0.1
led "colour by name" 202 '{"pattern":[{"color":"orange","ms":300},{"blue":200,"amber":100,"ms":300}]}'
led "while an LED sequence plays" 409 '{"pattern":[{"level":2,"ms":300}]}'
beep "beep beside an LED sequence" 202 '{"pattern":[{"tone":1}]}'
sleep 0.8
led "LED step of 249 ms" 400 '{"pattern":[{"level":2,"ms":249}]}'
led "level and colour together" 400 '{"pattern":[{"level":2,"color":"blue","ms":300}]}'
led "unknown colour" 400 '{"pattern":[{"color":"green","ms":300}]}'
led "steps over 10 s" 400 '{"pattern":[{"level":2,"ms":6000},{"level":1,"ms":4001}]}'
led "10 s times repeat" 400 '{"pattern":[{"level":2,"ms":4000}],"repeat":3}'
check "unknown path" 404 "$url/nothing"
check "GET /beep" 405 "$url/beep"
check "raw LED state (development build)" 200 "$url/indicator/raw"

# The held colour and the config API.
check "GET /config (default)" 200 "$url/config"
grep -q '"led_color": { "blue": 255, "white": 80, "amber": 200 }' "$work/body" && echo "ok   default config served" ||
    { echo "FAIL default config: $(cat "$work/body")"; failures=$((failures + 1)); }
check "POST /color" 202 -X POST "$url/color" -d '{"blue":100,"amber":30}'
check "status after /color" 200 "$url/status"
grep -q '"held":{"blue":100,"white":0,"amber":30}' "$work/body" && echo "ok   colour held" ||
    { echo "FAIL colour not held: $(cat "$work/body")"; failures=$((failures + 1)); }
led "a sequence over a held colour" 202 '{"pattern":[{"color":"white","ms":300}]}'
sleep 0.6
tail -3 "$work/log" | grep -q "led colour 100/0/30" && echo "ok   held colour back after the sequence" ||
    { echo "FAIL held colour not back: $(tail -3 "$work/log")"; failures=$((failures + 1)); }
check "POST /color system with a channel" 400 -X POST "$url/color" -d '{"system":true,"blue":1}'
check "POST /color system" 202 -X POST "$url/color" -d '{"system":true}'
check "status after system" 200 "$url/status"
grep -q '"held":null' "$work/body" && echo "ok   system colour again" ||
    { echo "FAIL still held: $(cat "$work/body")"; failures=$((failures + 1)); }
mkdir -p "$config"
check "PUT /config" 200 -X PUT "$url/config" -d '{"startup":{"beep":[{"tone":1}]},"led_color":{"white":200}}'
grep -q '"white":200' "$config/config.json" && echo "ok   config written" ||
    { echo "FAIL config not written"; failures=$((failures + 1)); }
check "status after PUT /config" 200 "$url/status"
grep -q '"held":{"blue":0,"white":200,"amber":0}' "$work/body" && echo "ok   saved colour held" ||
    { echo "FAIL saved colour not held: $(cat "$work/body")"; failures=$((failures + 1)); }
cp "$config/config.json" "$work/saved.json"
check "PUT /config, 25 startup beeps" 400 -X PUT "$url/config" -d "{\"startup\":{\"beep\":[$(printf '{"tone":1},%.0s' {1..24}){\"tone\":1}]}}"
check "PUT /config, colour 256" 400 -X PUT "$url/config" -d '{"led_color":{"blue":256}}'
cmp -s "$config/config.json" "$work/saved.json" && echo "ok   invalid config not written" ||
    { echo "FAIL invalid config written"; failures=$((failures + 1)); }
check "POST /config" 405 -X POST "$url/config" -d '{}'
# The beeper's volume.
check "POST /beeper volume 2" 200 -X POST "$url/beeper" -d '{"volume":2}'
check "POST /beeper volume 3" 400 -X POST "$url/beeper" -d '{"volume":3}'
check "status after /beeper" 200 "$url/status"
grep -q '"volume":2' "$work/body" && echo "ok   beeper volume reported" ||
    { echo "FAIL beeper volume: $(cat "$work/body")"; failures=$((failures + 1)); }
# The temperature on the LED (the fake sensors read 50 C: between cool 45 and the middle, blue to white).
check "status shows the temperature" 200 "$url/status"
grep -q '"temperature":{"cpu":50,"soc":50,"led":false' "$work/body" && echo "ok   temperature reported" ||
    { echo "FAIL temperature: $(cat "$work/body")"; failures=$((failures + 1)); }
check "PUT /config, temperature cool 60 warm 62" 400 -X PUT "$url/config" -d '{"temperature_led":{"cool":60,"warm":62}}'
check "PUT /config, temperature on" 200 -X PUT "$url/config" -d '{"led_color":{"blue":9,"white":9,"amber":9},"temperature_led":{"cool":45,"warm":70}}'
sleep 0.2
tail -8 "$work/log" | grep -q "led colour 149/106/0" && echo "ok   50 C shows between blue and white" ||
    { echo "FAIL temperature colour: $(tail -5 "$work/log")"; failures=$((failures + 1)); }
led "LED pattern while the temperature shows" 409 '{"pattern":[{"color":"blue","ms":300}]}'
check "POST /color while the temperature shows" 409 -X POST "$url/color" -d '{"blue":1}'
check "PUT /config, temperature off" 200 -X PUT "$url/config" -d '{"led_color":{"blue":9,"white":9,"amber":9},"temperature_led":null}'
sleep 0.2
tail -5 "$work/log" | grep -q "led colour 9/9/9" && echo "ok   the held colour again when the temperature stops" ||
    { echo "FAIL held colour after temperature: $(tail -5 "$work/log")"; failures=$((failures + 1)); }
rm -rf "$config"
grep -q "led state restored 0/255/0" "$work/log" && echo "ok   LED state put back" ||
    { echo "FAIL LED state not put back"; failures=$((failures + 1)); }

# The startup config, with the real main.c: the default written when there is none, and a config that isn't valid
# left as it is.
kill $server; wait $server 2>/dev/null || true
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -I "$root/tests/include" -DCONFIG_DIRECTORY="\"$config\"" -o "$work/main" \
    "$root/src/main.c" "${sources[@]}" -lpthread
"$work/main" 2>"$work/startup.log" & server=$!
sleep 2.6
kill $server; wait $server 2>/dev/null || true
[[ -f $config/config.json ]] && echo "ok   default config written" ||
    { echo "FAIL no default config"; failures=$((failures + 1)); }
[[ $(grep -c "\[fake\] beep [13]" "$work/startup.log") == 7 ]] && echo "ok   startup tune: 7 beeps" ||
    { echo "FAIL startup tune: $(grep -c "\[fake\] beep [13]" "$work/startup.log") beeps"; failures=$((failures + 1)); }
grep -q "led colour 255/0/0" "$work/startup.log" && grep -q "led colour 0/255/0" "$work/startup.log" &&
    echo "ok   startup LED: blue, white" || { echo "FAIL startup LED"; failures=$((failures + 1)); }
printf '{"startup":{"beep":[{"tone":9}]}}' > "$config/config.json"
cp "$config/config.json" "$work/invalid.json"
"$work/main" 2>"$work/invalid.log" & server=$!
sleep 0.5
kill $server; wait $server 2>/dev/null || true
cmp -s "$config/config.json" "$work/invalid.json" && ! grep -q "\[fake\] beep" "$work/invalid.log" &&
    echo "ok   invalid config left as it is, nothing played" ||
    { echo "FAIL invalid config"; failures=$((failures + 1)); }
exit $failures
