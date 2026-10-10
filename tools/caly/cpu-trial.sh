#!/system/bin/sh
set -eu
fail() { echo "ERROR: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
[ "$(getprop ro.product.device)" = Spacewar ] || fail 'Spacewar required'
[ "$(uname -r)" = 5.4.289-qgki-Caly-Rice-Beta ] || fail 'Rice required'
mode=${1:-responsive}
case "$mode" in responsive|balanced) ;; *) fail 'Use responsive or balanced';; esac
seconds=${2:-120}
case "$seconds" in ''|*[!0-9]*) fail 'Integer duration required';; esac
[ "$seconds" -ge 10 ] && [ "$seconds" -le 180 ] || fail 'Duration 10..180s'
[ "$(getprop sys.boot_completed)" = 1 ] || fail 'Wait for boot completion'
read -r temp < /sys/class/power_supply/battery/temp
[ "$temp" -lt 390 ] || fail 'Cool below 39 C'
base=/dev/cpuctl/top-app
for name in cpu.uclamp.min cpu.uclamp.max cpu.uclamp.latency_sensitive; do
  [ -w "$base/$name" ] || fail "Missing top-app $name"
done
minimum=$(cat "$base/cpu.uclamp.min")
maximum=$(cat "$base/cpu.uclamp.max")
awk -v n="$minimum" 'BEGIN {exit !(n+0 < 20)}' || fail 'Existing floor >=20%; keep ROM policy'
awk -v n="$maximum" 'BEGIN {exit !(n=="max" || n+0>=20)}' || fail 'Maximum below 20%'
lock=/data/local/tmp/caly-perf-trial.lock
mkdir "$lock" 2>/dev/null || fail 'Another trial or stale lock exists'
: > "$lock/nodes"
normalize() { cat "$1" | xargs; }
restore() {
  rc=$?
  trap - EXIT HUP INT TERM
  while IFS='|' read -r node old ours; do
    [ -n "$node" ] || continue
    if [ "$(normalize "$node")" = "$ours" ]; then
      printf '%s\n' "$old" > "$node" || rc=1
      echo "RESTORED $node=$(normalize "$node")"
    else
      echo "Keeping changed ROM value: $node=$(normalize "$node")"
    fi
  done < "$lock/nodes"
  rm -f "$lock/pid" "$lock/nodes"
  rmdir "$lock" || rc=1
  exit "$rc"
}
trap restore EXIT
trap 'exit 130' HUP INT TERM
printf '%s\n' "$$" > "$lock/pid"
change() {
  node=$1 value=$2 canonical=$3
  old=$(normalize "$node")
  # Journal before writing: TERM after a write still has rollback information.
  printf '%s|%s|%s\n' "$node" "$old" "$canonical" >> "$lock/nodes"
  printf '%s\n' "$value" > "$node"
  [ "$(normalize "$node")" = "$canonical" ] || fail "Write not effective: $node"
  echo "SET $node=$(normalize "$node")"
}
change "$base/cpu.uclamp.min" 20 20.00
change "$base/cpu.uclamp.latency_sensitive" 1 1
if [ "$mode" = balanced ]; then
  node=/sys/module/msm_performance/parameters/cpu_min_freq
  [ -w "$node" ] || fail 'Missing Qualcomm minimum request interface'
  [ "$(cat /sys/devices/system/cpu/present)" = 0-7 ] || fail 'Unexpected CPU topology'
  change "$node" '0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:0' '0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:0'
fi
echo "CPU_TRIAL_ACTIVE $mode"
remaining=$seconds
while [ "$remaining" -gt 0 ]; do
  sleep 2
  read -r temp < /sys/class/power_supply/battery/temp
  [ "$temp" -lt 390 ] || { echo 'Stopping at 39 C'; break; }
  remaining=$((remaining - 2))
done
