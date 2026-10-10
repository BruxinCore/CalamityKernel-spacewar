#!/system/bin/sh
# Run explicitly through adb/root. Never run at boot or from the installer.
set -eu
fail() { echo "ERROR: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
case "$(getprop ro.product.device)" in Spacewar|spacewar) ;; *) fail 'Spacewar required';; esac
case "$(uname -r)" in 5.4.289-qgki-Caly-Rice-Beta) ;; *) fail 'Matching Rice kernel required';; esac

read_node() {
  [ -r "$1" ] || return 0
  printf '\n[%s]\n' "$1"
  cat "$1"
}
snapshot() {
  uname -r
  read_node /proc/uptime
  read_node /proc/pressure/memory
  read_node /proc/pressure/cpu
  read_node /proc/pressure/io
  read_node /sys/kernel/mm/lru_gen/enabled
  read_node /sys/kernel/mm/lru_gen/min_ttl_ms
  read_node /sys/block/zram0/io_stat
  read_node /sys/block/zram0/mm_stat
  read_node /sys/block/zram0/comp_algorithm
  read_node /sys/class/power_supply/battery/temp
  read_node /proc/sys/vm/watermark_scale_factor
  read_node /proc/sys/vm/swappiness
  printf '\n[meminfo]\n'
  grep -E '^(MemAvailable|MemFree|Cached|SwapTotal|SwapFree):' /proc/meminfo
  printf '\n[reclaim counters]\n'
  grep -E '^(allocstall|pgscan|pgsteal|workingset|oom_kill|pswp)' /proc/vmstat
  printf '\n[LMKD properties]\n'
  for prop in ro.config.low_ram ro.lmk.use_psi ro.lmk.use_minfree_levels ro.lmk.psi_partial_stall_ms ro.lmk.psi_complete_stall_ms ro.lmk.thrashing_limit ro.lmk.swap_free_low_percentage ro.lmk.swap_util_max; do
    printf '%s=%s\n' "$prop" "$(getprop "$prop")"
  done
  printf '\n[cached app freezer policy]\n'
  settings get global cached_apps_freezer || true
  device_config get activity_manager_native_boot use_freezer || true
  # The settings can be null while the ROM defaults enable the feature.
  dumpsys activity settings | grep -E 'use_freezer=|use_compaction=|Apps frozen:|CUR_MAX_CACHED_PROCESSES=' || true
  printf '\n[LMKD PSI connections]\n'
  lmk_pid=$(pidof lmkd 2>/dev/null || true)
  case "$lmk_pid" in
    ''|*[!0-9]*) ;;
    *) ls -l "/proc/$lmk_pid/fd" 2>/dev/null | grep '/proc/pressure/' || true;;
  esac
  for root in /dev/cpuctl/top-app /sys/fs/cgroup/cpu/top-app; do
    read_node "$root/cpu.uclamp.min"
    read_node "$root/cpu.uclamp.max"
  done
  for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    read_node "$policy/scaling_governor"
    read_node "$policy/cpuinfo_min_freq"
    read_node "$policy/scaling_min_freq"
    read_node "$policy/scaling_max_freq"
    read_node "$policy/schedutil/up_rate_limit_us"
    read_node "$policy/schedutil/down_rate_limit_us"
  done
}

mode=${1:-snapshot}
case "$mode" in snapshot) snapshot; exit 0;; mglru-core|mglru-walk|top-app) ;; *) fail 'Use snapshot, mglru-core, mglru-walk or top-app';; esac
seconds=${2:-60}
case "$seconds" in ''|*[!0-9]*) fail 'Duration must be an integer';; esac
[ "$seconds" -ge 10 ] && [ "$seconds" -le 180 ] || fail 'Duration must be 10..180 seconds'
[ "$(getprop sys.boot_completed)" = 1 ] || fail 'Wait for Android boot completion'
read -r uptime rest < /proc/uptime
[ "${uptime%%.*}" -ge 240 ] || fail 'Wait four minutes for boot-time ZRAM/profile initialization'
read -r temp < /sys/class/power_supply/battery/temp
[ "$temp" -lt 390 ] || fail 'Cool phone below 39 C before comparison'

# Serialize trials; no permanent settings or root module are installed.
lock=/data/local/tmp/caly-perf-trial.lock
mkdir "$lock" 2>/dev/null || fail 'Another trial is active (or stale lock needs inspection)'
node= old= applied=
cleanup() {
  rc=$?
  trap - EXIT HUP INT TERM
  if [ -n "$applied" ]; then
    current=$(cat "$node")
    if [ "$current" = "$applied" ]; then
      if printf '%s\n' "$old" > "$node"; then
        echo "RESTORED $node=$(cat "$node")"
      else
        echo "ERROR: restoration failed: $node" >&2
        rc=1
      fi
    else
      echo "ROM/another tuner changed $node; keeping its value: $current"
    fi
  fi
  rm -f "$lock/pid" || rc=1
  rmdir "$lock" || rc=1
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
printf '%s\n' "$$" > "$lock/pid"

case "$mode" in
  mglru-core|mglru-walk)
    node=/sys/kernel/mm/lru_gen/enabled
    [ -w "$node" ] || fail 'MGLRU runtime interface unavailable'
    # TTL protection can provoke earlier OOM; do not combine it with this test.
    [ "$(cat /sys/kernel/mm/lru_gen/min_ttl_ms)" = 0 ] || fail 'Requires existing min_ttl_ms=0'
    old=$(cat "$node")
    [ "$((old & 1))" = 0 ] || fail 'MGLRU already active; collect baseline first'
    value=1
    [ "$mode" != mglru-walk ] || value=3
    ;;
  top-app)
    for candidate in /dev/cpuctl/top-app/cpu.uclamp.min /sys/fs/cgroup/cpu/top-app/cpu.uclamp.min; do
      [ -w "$candidate" ] || continue
      [ -z "$node" ] || fail 'Multiple top-app controllers; inspect ROM task profiles first'
      node=$candidate
    done
    [ -n "$node" ] || fail 'No recognized top-app CPU uclamp controller; inspect task_profiles.json'
    old=$(cat "$node")
    # This kernel accepts percent units, not raw 0..1024 utilization values.
    # Never lower an existing ROM floor or exceed its maximum clamp.
    awk -v n="$old" 'BEGIN {exit !(n+0 < 10)}' || fail 'ROM floor already >=10%; do not replace it'
    maximum=$(cat "${node%min}max")
    awk -v n="$maximum" 'BEGIN {exit !(n == "max" || n+0 >= 10)}' || fail 'ROM maximum below trial floor'
    value=10
    ;;
esac
echo "BASELINE mode=$mode duration=$seconds"
snapshot
# Mark the expected value before writing, so a signal right after the write
# still restores it. Both interfaces have stable canonical read formats.
case "$mode" in
  mglru-core) applied=0x0001;;
  mglru-walk) applied=0x0003;;
  top-app) applied=10.00;;
esac
printf '%s\n' "$value" > "$node"
actual=$(cat "$node")
requested=$applied
applied=$actual
[ "$actual" = "$requested" ] || fail "Requested value not effective: $node=$actual"
echo "TRIAL_ACTIVE $node=$actual"
remaining=$seconds
while [ "$remaining" -gt 0 ]; do
  sleep 2
  read -r temp < /sys/class/power_supply/battery/temp
  [ "$temp" -lt 390 ] || { echo 'Stopping: battery reached 39 C'; break; }
  remaining=$((remaining - 2))
done
echo 'TRIAL_END'
snapshot
