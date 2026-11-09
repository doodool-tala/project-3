#!/usr/bin/env bash
# Install, launch and play the debug APK on a running Android emulator.
#
# Usage: tools/emulator_smoke.sh [apk] [screenshot] [logcat]
#
# The test fails unless:
#   * the activity starts and its process survives,
#   * the custom panel reports a completed draw,
#   * a synthesized C3 touch reaches the native engine (noteOn 48),
#   * the Oboe stream opens and reports non-silent output (peak > 0.01),
#   * the screenshot is a real rendered frame, not a blank/black image.
set -Eeuo pipefail

PKG="com.b3organ.physical"
ACTIVITY="$PKG/.MainActivity"
APK="${1:-dist/B3Physical-debug.apk}"
SHOT="${2:-docs/emulator-panel.png}"
LOG="${3:-/tmp/b3_smoke_logcat.txt}"

mkdir -p "$(dirname "$SHOT")"

if command -v adb >/dev/null 2>&1; then
  ADB=(adb)
elif [ -n "${ANDROID_HOME:-}" ] && [ -x "$ANDROID_HOME/platform-tools/adb" ]; then
  ADB=("$ANDROID_HOME/platform-tools/adb")
elif [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -x "$ANDROID_SDK_ROOT/platform-tools/adb" ]; then
  ADB=("$ANDROID_SDK_ROOT/platform-tools/adb")
else
  echo "SMOKE FAIL: adb was not found" >&2
  exit 1
fi
if [ -n "${ANDROID_SERIAL:-}" ]; then
  ADB+=(-s "$ANDROID_SERIAL")
fi

log() { printf 'SMOKE: %s\n' "$*"; }

dump_logs() {
  timeout 30 "${ADB[@]}" logcat -d -v threadtime > "$LOG" 2>/dev/null || true
  timeout 30 "${ADB[@]}" logcat -d -b crash -v threadtime >> "$LOG" 2>/dev/null || true
  log "--- B3Engine / B3Audio / B3Ui / crash excerpt ---"
  grep -E 'B3Engine|B3Audio|B3Ui|FATAL EXCEPTION|ANR in' "$LOG" 2>/dev/null | tail -80 || true
}

fail() {
  echo "SMOKE FAIL: $*" >&2
  dump_logs
  exit 1
}

validate_png() {
  if ! python3 - "$SHOT" <<'PY'
import struct
import sys
with open(sys.argv[1], 'rb') as f:
    b = f.read()
if b[:8] != b'\x89PNG\r\n\x1a\n':
    raise SystemExit('not a PNG')
w, h = struct.unpack('>II', b[16:24])
if not b.endswith(b'IEND\xaeB`\x82'):
    raise SystemExit('truncated PNG')
if w <= 0 or h <= 0:
    raise SystemExit('invalid PNG dimensions')
print(w, h)
PY
  then
    fail "screenshot is not a complete PNG"
  fi
}

wait_for_text() {
  local needle="$1"
  local seconds="$2"
  local i
  for ((i = 0; i < seconds; i++)); do
    if timeout 20 "${ADB[@]}" logcat -d 2>/dev/null | grep -q "$needle"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

[ -f "$APK" ] || fail "APK not found: $APK"

log "device: $(timeout 15 "${ADB[@]}" get-serialno 2>/dev/null || true)"
if ! timeout 120 "${ADB[@]}" wait-for-device; then
  fail "no emulator device became available"
fi

log "waiting for Android boot"
boot=""
deadline=$((SECONDS + 900))
while [ "$SECONDS" -lt "$deadline" ]; do
  boot="$(timeout 20 "${ADB[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r\n' || true)"
  activity_service="$(timeout 20 "${ADB[@]}" shell service check activity 2>/dev/null | tr -d '\r' || true)"
  if [ "$boot" = "1" ] && [[ "$activity_service" != *"not found"* ]]; then
    break
  fi
  sleep 5
done
[ "$boot" = "1" ] || fail "Android did not finish booting (sys.boot_completed=$boot)"

for _ in $(seq 1 60); do
  if timeout 20 "${ADB[@]}" shell service check package 2>/dev/null | grep -qv 'not found'; then
    break
  fi
  sleep 2
done
if ! timeout 20 "${ADB[@]}" shell service check package 2>/dev/null | grep -qv 'not found'; then
  fail "package service is unavailable"
fi
if [ -z "$(timeout 20 "${ADB[@]}" shell pidof system_server 2>/dev/null | tr -d '\r\n' || true)" ]; then
  fail "system_server is not running"
fi

# A freshly booted AOSP emulator can leave the keyguard up. These are all
# best-effort; a locked user will make the activity launch fail below.
"${ADB[@]}" shell 'input keyevent KEYCODE_WAKEUP; input keyevent KEYCODE_MENU; wm dismiss-keyguard' \
  >/dev/null 2>&1 || true
# TCG boots can raise system ANR dialogs that cover the activity. Hide future
# dialogs and close any current system dialog before installing the APK.
timeout 20 "${ADB[@]}" shell settings put global hide_error_dialogs 1 >/dev/null 2>&1 || true
timeout 20 "${ADB[@]}" shell am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS \
  >/dev/null 2>&1 || true
sleep 1

log "installing $APK"
install_out="$(timeout 300 "${ADB[@]}" install -r "$APK" 2>&1 || true)"
printf '%s\n' "$install_out"
grep -q 'Success' <<<"$install_out" || fail "APK installation failed"

timeout 20 "${ADB[@]}" shell am force-stop "$PKG" >/dev/null 2>&1 || true
timeout 20 "${ADB[@]}" logcat -c >/dev/null 2>&1 || true

log "launching $ACTIVITY"
start_out="$(timeout 180 "${ADB[@]}" shell am start -W -n "$ACTIVITY" 2>&1 || true)"
printf '%s\n' "$start_out"
grep -Eq 'Status: (ok|OK)|Activity:' <<<"$start_out" || fail "activity did not start"

pid=""
for _ in $(seq 1 60); do
  pid="$(timeout 20 "${ADB[@]}" shell pidof "$PKG" 2>/dev/null | tr -d '\r\n' || true)"
  [ -n "$pid" ] && break
  sleep 1
done
[ -n "$pid" ] || fail "app process disappeared after launch (pid=$pid)"
log "app pid $pid"

log "waiting for the custom panel draw"
panel=""
for _ in $(seq 1 120); do
  if timeout 20 "${ADB[@]}" logcat -d -s B3Ui:I 2>/dev/null | grep -q 'panel drawn'; then
    panel=1
    break
  fi
  pid="$(timeout 20 "${ADB[@]}" shell pidof "$PKG" 2>/dev/null | tr -d '\r\n' || true)"
  [ -n "$pid" ] || fail "app process disappeared while waiting for panel draw"
  sleep 1
done
[ -n "$panel" ] || fail "panel never reported a completed draw"

if ! wait_for_text 'audio stream started' 30; then
  fail "Oboe audio stream never started"
fi
log "audio stream started"

if ! timeout 180 "${ADB[@]}" exec-out screencap -p > "$SHOT"; then
  fail "could not capture screenshot"
fi
shot_bytes="$(wc -c < "$SHOT" | tr -d ' ')"
[ "$shot_bytes" -gt 20000 ] || fail "screenshot is probably blank (${shot_bytes} bytes)"
if ! png_dims="$(validate_png)"; then
  fail "screenshot validation failed"
fi
read -r validated_w validated_h <<<"$png_dims"
log "panel screenshot: $SHOT (${shot_bytes} bytes, ${validated_w}x${validated_h})"

read -r screen_w screen_h < <(python3 - "$SHOT" <<'PY'
import struct
import sys
with open(sys.argv[1], 'rb') as f:
    header = f.read(33)
if header[:8] != b'\x89PNG\r\n\x1a\n':
    raise SystemExit('screenshot is not a PNG')
w, h = struct.unpack('>II', header[16:24])
print(w, h)
PY
)
[ "$screen_w" -gt 0 ] && [ "$screen_h" -gt 0 ] || fail "invalid screenshot dimensions"

# The panel is laid out on a fixed 1800x1000 virtual canvas. C3 is white key
# index 7; x is its centre, y is in the lower half of the white key so no black
# key overlaps it. The same transform as OrganView.onSizeChanged is used here.
read -r tap_x tap_y < <(python3 - "$screen_w" "$screen_h" <<'PY'
import sys
sw, sh = (int(v) for v in sys.argv[1:3])
scale = min(sw / 1800.0, sh / 1000.0)
off_x = (sw - 1800.0 * scale) / 2.0
off_y = (sh - 1000.0 * scale) / 2.0
white_w = (1766.0 - 34.0) / 36.0
x = 34.0 + 7.5 * white_w
y = 900.0
print(round(off_x + x * scale), round(off_y + y * scale))
PY
)
[ "$tap_x" -gt 0 ] && [ "$tap_x" -lt "$screen_w" ] || fail "bad tap x=$tap_x"
[ "$tap_y" -gt 0 ] && [ "$tap_y" -lt "$screen_h" ] || fail "bad tap y=$tap_y"
log "tapping C3 at ${tap_x},${tap_y}"
# The adb client can time out on a TCG host even after the guest delivered the
# gesture. Treat the JNI/audio log assertions below as authoritative; a tap
# that never arrived will fail those assertions.
timeout 60 "${ADB[@]}" shell input tap "$tap_x" "$tap_y" >/dev/null 2>&1 || true
sleep 2

dump_logs

python3 - "$LOG" <<'PY' || fail "on-device evidence assertions failed"
import re
import sys

text = open(sys.argv[1], errors='replace').read()
required = [
    'panel drawn',
    'noteOn 48',
    'audio stream started',
]
missing = [needle for needle in required if needle not in text]
if missing:
    raise SystemExit('missing log evidence: ' + ', '.join(missing))

peaks = [float(m) for m in re.findall(r'B3Audio.*?level peak=([0-9.]+)', text)]
if not peaks:
    raise SystemExit('no B3Audio level diagnostics were emitted')
if max(peaks) <= 0.01:
    raise SystemExit('native audio output stayed silent (max peak %.4f)' % max(peaks))

bad = [line for line in ('FATAL EXCEPTION', 'ANR in com.b3organ.physical') if line in text]
if bad:
    raise SystemExit('app failure in logcat: ' + ', '.join(bad))

print('native audio peak: %.4f across %d level reports' % (max(peaks), len(peaks)))
print('panel, touch -> C3 and native audio evidence present')
PY

if ! timeout 20 "${ADB[@]}" shell pidof "$PKG" 2>/dev/null | grep -q .; then
  fail "app process did not survive the interaction"
fi

shot_bytes="$(wc -c < "$SHOT" | tr -d ' ')"
[ "$shot_bytes" -gt 20000 ] || fail "final screenshot is probably blank (${shot_bytes} bytes)"
log "PASS: ${screen_w}x${screen_h} panel, C3 at ${tap_x},${tap_y}, screenshot ${shot_bytes} bytes"
