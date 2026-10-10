# Rice: controlled performance trials

These are manual, temporary experiments, not boot scripts or proven defaults.
Run `perf-trial.sh` on Spacewar through a root shell. `snapshot` is read-only.
Trial duration is 10..180 seconds (default 60); settings are restored on normal
completion, HUP, INT or TERM. A reboot also returns these runtime nodes to their
boot policies. SIGKILL cannot run cleanup: inspect the saved baseline output
and restore the node, or reboot, before proceeding. A stale lock must be
inspected before removing `/data/local/tmp/caly-perf-trial.lock` with `rmdir`.
For early termination, send TERM to the exact shell PID recorded in
`/data/local/tmp/caly-perf-trial.lock/pid`; avoid broad process-name matching.
After a reboot/SIGKILL, confirm that the recorded PID is no longer the trial
before deleting its `pid` file and removing the stale directory.

1. `snapshot`: memory PSI, reclaim/ZRAM counters, LMKD properties, freezer
   policy, top-app clamps and cpufreq limits. An empty property or `null`
   does not prove that a feature is off: Android may use compiled defaults.
2. `mglru-core 60`: enable only the main MGLRU switch, after four minutes of
   uptime. `mglru-walk 60` additionally requests batched leaf-PTE accessed-bit
   scanning. It fails if hardware does not expose the requested capability.
   Both require MGLRU initially disabled and existing `min_ttl_ms=0`.
3. `top-app 60`: test a 10% CPU uclamp minimum for the recognized top-app CPU
   group, only if the ROM's existing floor is lower. This affects runnable
   top-app tasks, not a permanent CPU frequency minimum. Thermal/frequency
   limits, background groups and input boost remain under existing policies.
   It fails if the group is missing or ambiguous. Do not substitute arbitrary
   schedtune nodes: they have different semantics.

Run one trial at a time, with the same apps, cache conditions, display mode and
cool temperature. Collect baseline and trial launch/frame timings, memory PSI,
app restarts, ZRAM failed_writes, and ANR/kernel logs. ZRAM resets clear its
counters: compare only intervals with matching disk configuration. A short
test without a freeze cannot establish that a random freeze is fixed.

## Audit of this tree

The MGLRU integration arrived in `981614c4f098` (Nothing OS 3.0 source update),
not a recent Rice performance commit. `67cec720d872` generated a full config
with `CONFIG_LRU_GEN=y`, `CONFIG_LRU_GEN_ENABLED=n`, and stats disabled.
The working Rice artifact retains those settings. The available history did
not identify the user's previous failing MGLRU trial; its cause remains unknown.
Presence of context-switch/fork hooks and ARM64 hardware-access-flag plumbing
is verified, but is not a full correctness proof of the backport.

The vendor WALT branch of `sugov_get_util()` already calls
`uclamp_rq_util_with()`. The cgroup clamp parser takes percentages, so writing
raw capacity values (for example 128) would be invalid. ROM task profiles,
ancestor clamps, PowerHAL, FKM overrides and active task membership must still
be checked on the actual device before selecting a persistent policy.

PSI, memcg/swap, userspace-LMK support and modern Binder freezer interfaces
already exist. Before changing LMKD or cached-app freezer policy, inspect ROM
task/cgroup profiles, `dumpsys activity settings`, cached-process frozen states
and `dumpsys activity exit-info` locally. Keep personal app/log details private.
Do not restart LMKD, force freezer settings or increase cached-process limits
without a baseline: those actions can introduce kills or memory pressure.
The current reported boot-time ZRAM write failures remain unresolved.

## Device check, 2026-10-10

On Nothing OS 3.2 / Android 15 with the working Rice kernel and built-in KSU,
both MGLRU masks (1 and 3) were accepted. Short launch/scroll/switch trials and
a bounded memory-load trial completed without an observed reboot or full
device freeze; original nodes were restored. This does not establish long-term
stability. ZRAM failed_reads/failed_writes remained zero in this session.

Measurements did not establish a consistent UI improvement. App reloads due
to LOW_MEMORY occurred during MGLRU trials, and the memory probe reached
different allocation sizes before its availability guard, so those runs are
not equivalent performance comparisons. Keep MGLRU off by default.
The top-app 10% clamp also did not establish a consistent improvement with
the device's existing high CPU frequency floors; retain the original policy.

The ROM already had active cached-app freezer/compaction and LMKD connections
to memory PSI. A Binder freezer-related app exit was recorded before the
MGLRU trials. It is an app reload lead, not proof of a system-wide freeze cause.
Do not force freezer or LMKD changes merely because their property overrides
are empty. Baseline cache-process limits were retained; no ROM restart, app
data clearing, thermal bypass or persistent new tunable was performed.

Primary references:
- https://docs.kernel.org/admin-guide/mm/multigen_lru.html
- https://docs.kernel.org/scheduler/sched-util-clamp.html
- https://source.android.com/docs/core/perf/lmkd
- https://source.android.com/docs/core/perf/cached-apps-freezer
- https://source.android.com/docs/core/perf/cgroups

## CPU profile candidate

`cpu-trial.sh responsive 120` tests a 20% top-app utilization floor together
with `cpu.uclamp.latency_sensitive=1`. Unlike the earlier 10% clamp-only
experiment, this also exercises the kernel's idle/spare-capacity placement
path in `find_energy_efficient_cpu()`. It does not guarantee faster launches.

`cpu-trial.sh balanced 120` additionally clears Qualcomm's per-CPU minimum
frequency requests in `msm_performance`, retaining frequency maximums,
thermal control, the CPU governor and input boost. The observed high floors
were Qualcomm requests (not demonstrated to be FKM settings). Readback can
remain high if a different client requests it; do not override that client.
Both variants were subsequently exercised on Spacewar / Nothing OS 3.2 with
Rice and KSU UAPI 5. They remain beta candidates, not proven speedups.

The Daily Profile v0.4 includes canonical `cpu-policy.sh` and a `cpu-mode`
file. Valid choices are `legacy`, `responsive` and `balanced`. Packaged default
is `legacy` until device comparison. After a tested mode is selected, apply it
through the module's `apply.sh`. The helper records previous values, restores
them before changing modes/on uninstall, skips values changed by another
tuner and rejects restoration from a different boot. It serializes with the
temporary trials. Disabling the module and rebooting restores ROM boot policy.
There is no background loop fighting PowerHAL requests or a ZRAM/MGLRU change.

### CPU comparison, 2026-10-10

At 37-38 C battery temperature, the legacy, responsive and balanced modes
completed sequential app launch/switch/bidirectional-scroll runs followed by
three cold Settings launches and scroll samples per mode. No device freeze
or reboot was observed; ZRAM failed reads/writes remained zero. The timed
helpers restored top-app nodes and preserved frequency requests rewritten
by the ROM. This cannot establish that the intermittent freeze is fixed.

Settings cold-launch medians were 411 / 403 / 419 ms respectively. Frame-time
p95 samples were 9,5,7 / 5,6,6 / 5,6,6 ms. Differences do not establish a
consistent launch improvement. Social-app runs had different hot/cold states
and content; WhatsApp produced too few frames for a useful scroll comparison.

During the eight-second screen-on idle interval, balanced spent 92.7%, 94.5%
and 91.7% of cpufreq residency below the previous little/big/prime floors;
legacy and responsive spent 0%. This verifies lower idle frequencies during
that interval, not battery savings or sustained behavior. The ROM rewrote
msm_performance requests during the subsequent app workload. Its later
requests are respected; the module does not continuously reassert zero.

The tested phone selects balanced in Daily Profile v0.4 for reversible beta
use. Distributed cpu-mode remains legacy so this one-device short experiment
does not silently become the default for other users. All private device logs
and app data remain outside Git. No kernel flash is needed for these tunables.
