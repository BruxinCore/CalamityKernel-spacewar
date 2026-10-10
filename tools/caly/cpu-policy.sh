#!/system/bin/sh
# Canonical policy helper copied into the Daily Profile by CalyBuild.
set -eu
MODDIR=${0%/*}
state="$MODDIR/.cpu-state"
bootfile="$MODDIR/.cpu-boot-id"
boot=$(cat /proc/sys/kernel/random/boot_id)
normalize() { cat "$1" | xargs; }
restore() {
  [ -f "$state" ] || return 0
  if [ "$(cat "$bootfile" 2>/dev/null)" = "$boot" ]; then
    while IFS='|' read -r node old ours; do
      [ -w "$node" ] || continue
      if [ "$(normalize "$node")" = "$ours" ]; then
        printf '%s\n' "$old" > "$node" || return 1
      fi
    done < "$state"
  fi
  rm -f "$state" "$bootfile"
}
mode=${1:-$(cat "$MODDIR/cpu-mode" 2>/dev/null || echo legacy)}
case "$mode" in legacy|responsive|balanced|restore) ;; *) echo 'Invalid Caly CPU mode' >&2; exit 1;; esac
# Rollback is permitted independently of root/kernel guards at uninstall.
[ "$(id -u)" = 0 ] || exit 1
lock=/data/local/tmp/caly-perf-trial.lock
mkdir "$lock" 2>/dev/null || { echo 'Another Caly CPU operation is active' >&2; exit 1; }
unlock() { rm -f "$lock/pid"; rmdir "$lock"; }
trap unlock EXIT
trap 'exit 130' HUP INT TERM
printf '%s\n' "$$" > "$lock/pid"
restore
case "$mode" in legacy|restore) exit 0;; esac
[ "$(getprop ro.product.device)" = Spacewar ] || exit 1
[ "$(uname -r)" = 5.4.289-qgki-Caly-Rice-Beta ] || exit 1
[ "$(getprop sys.boot_completed)" = 1 ] || exit 1
base=/dev/cpuctl/top-app
for name in cpu.uclamp.min cpu.uclamp.max cpu.uclamp.latency_sensitive; do
  [ -w "$base/$name" ] || exit 1
done
minimum=$(normalize "$base/cpu.uclamp.min")
maximum=$(normalize "$base/cpu.uclamp.max")
# Never lower an existing ROM clamp, or write a floor over the ROM maximum.
awk -v n="$minimum" 'BEGIN {exit !(n+0<=20)}' || exit 1
awk -v n="$maximum" 'BEGIN {exit !(n=="max" || n+0>=20)}' || exit 1
if [ "$mode" = balanced ]; then
  [ "$(cat /sys/devices/system/cpu/present)" = 0-7 ] || exit 1
  [ -w /sys/module/msm_performance/parameters/cpu_min_freq ] || exit 1
fi
: > "$state"
printf '%s\n' "$boot" > "$bootfile"
rollback() { rc=$?; trap - EXIT HUP INT TERM; restore || rc=1; unlock || rc=1; exit "$rc"; }
trap rollback EXIT
trap 'exit 130' HUP INT TERM
change() {
  node=$1 value=$2 canonical=$3
  printf '%s|%s|%s\n' "$node" "$(normalize "$node")" "$canonical" >> "$state"
  printf '%s\n' "$value" > "$node"
  [ "$(normalize "$node")" = "$canonical" ]
}
change "$base/cpu.uclamp.min" 20 20.00
change "$base/cpu.uclamp.latency_sensitive" 1 1
if [ "$mode" = balanced ]; then
  change /sys/module/msm_performance/parameters/cpu_min_freq '0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:0' '0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:0'
fi
# Success leaves the selected policy active. Restore operates only on values
# still owned by this module, and never restores a previous boot's requests.
trap unlock EXIT
echo "Caly CPU policy applied: $mode"
