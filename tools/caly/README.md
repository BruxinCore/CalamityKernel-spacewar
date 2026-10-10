# Rice: controlled performance trials

These are manual, temporary experiments, not boot scripts or proven defaults.
Run `perf-trial.sh` on Spacewar through a root shell. `snapshot` is read-only.
Trial duration is 10..180 seconds (default 60); settings are restored on normal
completion, HUP, INT or TERM. A reboot also returns these runtime nodes to their
boot policies. SIGKILL cannot run cleanup: inspect the saved baseline output
and restore the node, or reboot, before proceeding. A stale lock must be
inspected before removing `/data/local/tmp/caly-perf-trial.lock` with `rmdir`.

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

Primary references:
- https://docs.kernel.org/admin-guide/mm/multigen_lru.html
- https://docs.kernel.org/scheduler/sched-util-clamp.html
- https://source.android.com/docs/core/perf/lmkd
- https://source.android.com/docs/core/perf/cached-apps-freezer
- https://source.android.com/docs/core/perf/cgroups
