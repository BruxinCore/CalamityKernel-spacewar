#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "sched.h"

#include <linux/sched/cpufreq.h>
#include <trace/events/power.h>
#include <linux/sched/sysctl.h>
#include <trace/hooks/sched.h>

#define IOWAIT_BOOST_MIN	(SCHED_CAPACITY_SCALE / 8)

struct sugov_tunables {
	struct gov_attr_set	attr_set;
	unsigned int		up_rate_limit_us;
	unsigned int		down_rate_limit_us;
	unsigned int		hispeed_load;
	unsigned int		hispeed_freq;
	unsigned int		rtg_boost_freq;
	bool			pl;
};

struct sugov_policy {
	struct cpufreq_policy	*policy;

	u64 last_ws;
	u64 curr_cycles;
	u64 last_cyc_update_time;
	unsigned long avg_cap;
	struct sugov_tunables	*tunables;
	struct list_head	tunables_hook;
	unsigned long hispeed_util;
	unsigned long rtg_boost_util;
	unsigned long max;

	raw_spinlock_t		update_lock;
	u64			last_freq_update_time;
	s64			min_rate_limit_ns;
	s64			up_rate_delay_ns;
	s64			down_rate_delay_ns;
	unsigned int		next_freq;
	unsigned int		cached_raw_freq;

	struct			irq_work irq_work;
	struct			kthread_work work;
	struct			mutex work_lock;
	struct			kthread_worker worker;
	struct task_struct	*thread;
	bool			work_in_progress;

	bool			limits_changed;
	bool			need_freq_update;
};

struct sugov_cpu {
	struct update_util_data	update_util;
	struct sugov_policy	*sg_policy;
	unsigned int		cpu;

	bool			iowait_boost_pending;
	unsigned int		iowait_boost;
	u64			last_update;

	struct walt_cpu_load	walt_load;

	unsigned long util;
	unsigned int flags;

	unsigned long		bw_dl;
	unsigned long		max;

#ifdef CONFIG_NO_HZ_COMMON
	unsigned long		saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct sugov_cpu, sugov_cpu);
static unsigned int stale_ns;
static DEFINE_PER_CPU(struct sugov_tunables *, cached_tunables);

static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	if (!cpufreq_this_cpu_can_update(sg_policy->policy))
		return false;

	if (unlikely(READ_ONCE(sg_policy->limits_changed))) {
		WRITE_ONCE(sg_policy->limits_changed, false);
		sg_policy->need_freq_update = true;

		smp_mb();

		return true;
	}

	delta_ns = time - sg_policy->last_freq_update_time;
	return delta_ns >= sg_policy->min_rate_limit_ns;
}

static bool sugov_up_down_rate_limit(struct sugov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;

	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static inline bool use_pelt(void)
{
#ifdef CONFIG_SCHED_WALT
	return false;
#else
	return true;
#endif
}

static inline bool conservative_pl(void)
{
#ifdef CONFIG_SCHED_WALT
	return sysctl_sched_conservative_pl;
#else
	return false;
#endif
}

static bool sugov_update_next_freq(struct sugov_policy *sg_policy, u64 time,
				   unsigned int next_freq)
{
	if (sg_policy->next_freq == next_freq)
		return false;

	if (sugov_up_down_rate_limit(sg_policy, time, next_freq))
		return false;

	sg_policy->next_freq = next_freq;
	sg_policy->last_freq_update_time = time;

	return true;
}

static unsigned long freq_to_util(struct sugov_policy *sg_policy,
				  unsigned int freq)
{
	return mult_frac(sg_policy->max, freq,
			 sg_policy->policy->cpuinfo.max_freq);
}

#define KHZ 1000
static void sugov_track_cycles(struct sugov_policy *sg_policy,
				unsigned int prev_freq,
				u64 upto)
{
	u64 delta_ns, cycles;
	u64 next_ws = sg_policy->last_ws + sched_ravg_window;

	if (use_pelt())
		return;

	upto = min(upto, next_ws);

	delta_ns = upto - sg_policy->last_cyc_update_time;
	delta_ns *= prev_freq;
	do_div(delta_ns, (NSEC_PER_SEC / KHZ));
	cycles = delta_ns;
	sg_policy->curr_cycles += cycles;
	sg_policy->last_cyc_update_time = upto;
}

static void sugov_calc_avg_cap(struct sugov_policy *sg_policy, u64 curr_ws,
				unsigned int prev_freq)
{
	u64 last_ws = sg_policy->last_ws;
	unsigned int avg_freq;

	if (use_pelt())
		return;

	BUG_ON(curr_ws < last_ws);
	if (curr_ws <= last_ws)
		return;

	if (curr_ws > (last_ws + sched_ravg_window)) {
		avg_freq = prev_freq;

		sg_policy->last_cyc_update_time = curr_ws;
	} else {
		sugov_track_cycles(sg_policy, prev_freq, curr_ws);
		avg_freq = sg_policy->curr_cycles;
		avg_freq /= sched_ravg_window / (NSEC_PER_SEC / KHZ);
	}
	sg_policy->avg_cap = freq_to_util(sg_policy, avg_freq);
	sg_policy->curr_cycles = 0;
	sg_policy->last_ws = curr_ws;
}

static void sugov_fast_switch(struct sugov_policy *sg_policy, u64 time,
			      unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	int cpu;

	if (!sugov_update_next_freq(sg_policy, time, next_freq))
		return;

	sugov_track_cycles(sg_policy, sg_policy->policy->cur, time);
	next_freq = cpufreq_driver_fast_switch(policy, next_freq);
	if (!next_freq)
		return;

	policy->cur = next_freq;

	if (trace_cpu_frequency_enabled()) {
		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(next_freq, cpu);
	}
}

static void sugov_deferred_update(struct sugov_policy *sg_policy, u64 time,
				  unsigned int next_freq)
{
	if (!sugov_update_next_freq(sg_policy, time, next_freq))
		return;

	if (use_pelt())
		sg_policy->work_in_progress = true;
	walt_irq_work_queue(&sg_policy->irq_work);
}

#define TARGET_LOAD 80

static unsigned int get_next_freq(struct sugov_policy *sg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;
	unsigned long next_freq = 0;

	trace_android_vh_map_util_freq(util, freq, max, &next_freq);
	if (next_freq)
		freq = next_freq;
	else
		freq = map_util_freq(util, freq, max);

	trace_sugov_next_freq(policy->cpu, util, max, freq);
	if (freq == sg_policy->cached_raw_freq && !sg_policy->need_freq_update)
		return sg_policy->next_freq;

	sg_policy->need_freq_update = false;
	sg_policy->cached_raw_freq = freq;
	return cpufreq_driver_resolve_freq(policy, freq);
}

unsigned long schedutil_cpu_util(int cpu, unsigned long util_cfs,
				 unsigned long max, enum schedutil_type type,
				 struct task_struct *p)
{
	unsigned long dl_util, util, irq;
	struct rq *rq = cpu_rq(cpu);

	if (!uclamp_is_used() && sched_feat(SUGOV_RT_MAX_FREQ) &&
	    type == FREQUENCY_UTIL && rt_rq_is_runnable(&rq->rt)) {
		return max;
	}

	irq = cpu_util_irq(rq);
	if (unlikely(irq >= max))
		return max;

	util = util_cfs + cpu_util_rt(rq);
	if (type == FREQUENCY_UTIL)
		util = uclamp_rq_util_with(rq, util, p);

	dl_util = cpu_util_dl(rq);

	if (util + dl_util >= max)
		return max;

	if (type == ENERGY_UTIL)
		util += dl_util;

	util = scale_irq_capacity(util, irq, max);
	util += irq;

	if (type == FREQUENCY_UTIL)
		util += cpu_bw_dl(rq);

	return min(max, util);
}

static unsigned long sugov_get_util(struct sugov_cpu *sg_cpu)
{
	struct rq *rq = cpu_rq(sg_cpu->cpu);
	unsigned long max = arch_scale_cpu_capacity(sg_cpu->cpu);
	unsigned long util;

	sg_cpu->max = max;
	sg_cpu->bw_dl = cpu_bw_dl(rq);

#ifdef CONFIG_SCHED_WALT
	util = cpu_util_freq_walt(sg_cpu->cpu, &sg_cpu->walt_load);

	return uclamp_rq_util_with(rq, util, NULL);
#else
	util = cpu_util_cfs(rq);

	return schedutil_cpu_util(sg_cpu->cpu, util, max, FREQUENCY_UTIL, NULL);
#endif
}

static bool sugov_iowait_reset(struct sugov_cpu *sg_cpu, u64 time,
			       bool set_iowait_boost)
{
	s64 delta_ns = time - sg_cpu->last_update;

	if (delta_ns <= TICK_NSEC)
		return false;

	sg_cpu->iowait_boost = set_iowait_boost ? IOWAIT_BOOST_MIN : 0;
	sg_cpu->iowait_boost_pending = set_iowait_boost;

	return true;
}

static void sugov_iowait_boost(struct sugov_cpu *sg_cpu, u64 time,
			       unsigned int flags)
{
	bool set_iowait_boost = flags & SCHED_CPUFREQ_IOWAIT;

	if (sg_cpu->iowait_boost &&
	    sugov_iowait_reset(sg_cpu, time, set_iowait_boost))
		return;

	if (!set_iowait_boost)
		return;

	if (sg_cpu->iowait_boost_pending)
		return;
	sg_cpu->iowait_boost_pending = true;

	if (sg_cpu->iowait_boost) {
		sg_cpu->iowait_boost =
			min_t(unsigned int, sg_cpu->iowait_boost << 1, SCHED_CAPACITY_SCALE);
		return;
	}

	sg_cpu->iowait_boost = IOWAIT_BOOST_MIN;
}

static unsigned long sugov_iowait_apply(struct sugov_cpu *sg_cpu, u64 time,
					unsigned long util, unsigned long max)
{
	unsigned long boost;

	if (!sg_cpu->iowait_boost)
		return util;

	if (sugov_iowait_reset(sg_cpu, time, false))
		return util;

	if (!sg_cpu->iowait_boost_pending) {

		sg_cpu->iowait_boost >>= 1;
		if (sg_cpu->iowait_boost < IOWAIT_BOOST_MIN) {
			sg_cpu->iowait_boost = 0;
			return util;
		}
	}

	sg_cpu->iowait_boost_pending = false;

	boost = (sg_cpu->iowait_boost * max) >> SCHED_CAPACITY_SHIFT;
	return max(boost, util);
}

#ifdef CONFIG_NO_HZ_COMMON
static bool sugov_cpu_is_busy(struct sugov_cpu *sg_cpu)
{
	unsigned long idle_calls = tick_nohz_get_idle_calls_cpu(sg_cpu->cpu);
	bool ret = idle_calls == sg_cpu->saved_idle_calls;

	sg_cpu->saved_idle_calls = idle_calls;
	return ret;
}
#else
static inline bool sugov_cpu_is_busy(struct sugov_cpu *sg_cpu) { return false; }
#endif

#define NL_RATIO 75
#define DEFAULT_HISPEED_LOAD 90
#define DEFAULT_CPU0_RTG_BOOST_FREQ 1000000
#define DEFAULT_CPU4_RTG_BOOST_FREQ 0
#define DEFAULT_CPU7_RTG_BOOST_FREQ 0
static void sugov_walt_adjust(struct sugov_cpu *sg_cpu, unsigned long *util,
			      unsigned long *max)
{
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	bool is_migration = sg_cpu->flags & SCHED_CPUFREQ_INTERCLUSTER_MIG;
	bool is_rtg_boost = sg_cpu->walt_load.rtgb_active;
	unsigned long nl = sg_cpu->walt_load.nl;
	unsigned long cpu_util = sg_cpu->util;
	bool is_hiload;
	unsigned long pl = sg_cpu->walt_load.pl;

	if (use_pelt())
		return;

	if (is_rtg_boost)
		*util = max(*util, sg_policy->rtg_boost_util);

	is_hiload = (cpu_util >= mult_frac(sg_policy->avg_cap,
					   sg_policy->tunables->hispeed_load,
					   100));

	if (is_hiload && !is_migration)
		*util = max(*util, sg_policy->hispeed_util);

	if (is_hiload && nl >= mult_frac(cpu_util, NL_RATIO, 100))
		*util = *max;

	if (sg_policy->tunables->pl) {
		if (conservative_pl())
			pl = mult_frac(pl, TARGET_LOAD, 100);
		*util = max(*util, pl);
	}
}

static inline void ignore_dl_rate_limit(struct sugov_cpu *sg_cpu, struct sugov_policy *sg_policy)
{
	if (cpu_bw_dl(cpu_rq(sg_cpu->cpu)) > sg_cpu->bw_dl)
		WRITE_ONCE(sg_policy->limits_changed, true);
}

static inline unsigned long target_util(struct sugov_policy *sg_policy,
				  unsigned int freq)
{
	unsigned long util;

	util = freq_to_util(sg_policy, freq);
	util = mult_frac(util, TARGET_LOAD, 100);
	return util;
}

static void sugov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned long util, max, hs_util, boost_util;
	unsigned int next_f;
	bool busy;

	if (!sg_policy->tunables->pl && flags & SCHED_CPUFREQ_PL)
		return;

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	ignore_dl_rate_limit(sg_cpu, sg_policy);

	if (!sugov_should_update_freq(sg_policy, time))
		return;

	busy = use_pelt() && !sg_policy->need_freq_update &&
		sugov_cpu_is_busy(sg_cpu);

	sg_cpu->util = util = sugov_get_util(sg_cpu);
	max = sg_cpu->max;
	sg_cpu->flags = flags;

	if (sg_policy->max != max) {
		sg_policy->max = max;
		hs_util = target_util(sg_policy,
				       sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;

		boost_util = target_util(sg_policy,
				    sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
	}

	util = sugov_iowait_apply(sg_cpu, time, util, max);
	sugov_calc_avg_cap(sg_policy, sg_cpu->walt_load.ws,
			   sg_policy->policy->cur);

	trace_sugov_util_update(sg_cpu->cpu, sg_cpu->util,
				sg_policy->avg_cap, max, sg_cpu->walt_load.nl,
				sg_cpu->walt_load.pl,
				sg_cpu->walt_load.rtgb_active, flags);

	sugov_walt_adjust(sg_cpu, &util, &max);
	next_f = get_next_freq(sg_policy, util, max);

	if (busy && next_f < sg_policy->next_freq) {
		next_f = sg_policy->next_freq;

		sg_policy->cached_raw_freq = 0;
	}

	if (sg_policy->policy->fast_switch_enabled) {
		sugov_fast_switch(sg_policy, time, next_f);
	} else {
		raw_spin_lock(&sg_policy->update_lock);
		sugov_deferred_update(sg_policy, time, next_f);
		raw_spin_unlock(&sg_policy->update_lock);
	}
}

static unsigned int sugov_next_freq_shared(struct sugov_cpu *sg_cpu, u64 time)
{
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	u64 last_freq_update_time = sg_policy->last_freq_update_time;
	unsigned long util = 0, max = 1;
	unsigned int j;

	for_each_cpu(j, policy->cpus) {
		struct sugov_cpu *j_sg_cpu = &per_cpu(sugov_cpu, j);
		unsigned long j_util, j_max;
		s64 delta_ns;

		delta_ns = last_freq_update_time - j_sg_cpu->last_update;
		if (delta_ns > stale_ns) {
			sugov_iowait_reset(j_sg_cpu, last_freq_update_time,
					   false);
			continue;
		}

		j_util = j_sg_cpu->util;
		j_max = j_sg_cpu->max;
		j_util = sugov_iowait_apply(j_sg_cpu, time, j_util, j_max);

		if (j_util * max >= j_max * util) {
			util = j_util;
			max = j_max;
		}

		sugov_walt_adjust(j_sg_cpu, &util, &max);
	}

	return get_next_freq(sg_policy, util, max);
}

static void
sugov_update_shared(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned long hs_util, boost_util;
	unsigned int next_f;

	if (!sg_policy->tunables->pl && flags & SCHED_CPUFREQ_PL)
		return;

	sg_cpu->util = sugov_get_util(sg_cpu);
	sg_cpu->flags = flags;
	raw_spin_lock(&sg_policy->update_lock);

	if (sg_policy->max != sg_cpu->max) {
		sg_policy->max = sg_cpu->max;
		hs_util = target_util(sg_policy,
					sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;

		boost_util = target_util(sg_policy,
				    sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
	}

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	sugov_calc_avg_cap(sg_policy, sg_cpu->walt_load.ws,
			   sg_policy->policy->cur);
	ignore_dl_rate_limit(sg_cpu, sg_policy);

	trace_sugov_util_update(sg_cpu->cpu, sg_cpu->util, sg_policy->avg_cap,
				sg_cpu->max, sg_cpu->walt_load.nl,
				sg_cpu->walt_load.pl,
				sg_cpu->walt_load.rtgb_active, flags);

	if (sugov_should_update_freq(sg_policy, time) &&
	    !(flags & SCHED_CPUFREQ_CONTINUE)) {
		next_f = sugov_next_freq_shared(sg_cpu, time);

		if (sg_policy->policy->fast_switch_enabled)
			sugov_fast_switch(sg_policy, time, next_f);
		else
			sugov_deferred_update(sg_policy, time, next_f);
	}

	raw_spin_unlock(&sg_policy->update_lock);
}

static void sugov_work(struct kthread_work *work)
{
	struct sugov_policy *sg_policy = container_of(work, struct sugov_policy, work);
	unsigned int freq;
	unsigned long flags;

	raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
	freq = sg_policy->next_freq;
	if (use_pelt())
		sg_policy->work_in_progress = false;
	sugov_track_cycles(sg_policy, sg_policy->policy->cur,
			   ktime_get_ns());
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);
}

static void sugov_irq_work(struct irq_work *irq_work)
{
	struct sugov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct sugov_policy, irq_work);

	kthread_queue_work(&sg_policy->worker, &sg_policy->work);
}

static struct sugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct sugov_tunables *to_sugov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct sugov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_ns(struct sugov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	sg_policy->min_rate_limit_ns = min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static ssize_t hispeed_load_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_load);
}

static ssize_t hispeed_load_store(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->hispeed_load))
		return -EINVAL;

	tunables->hispeed_load = min(100U, tunables->hispeed_load);

	return count;
}

static ssize_t hispeed_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_freq);
}

static ssize_t hispeed_freq_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	unsigned int val;
	struct sugov_policy *sg_policy;
	unsigned long hs_util;
	unsigned long flags;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	tunables->hispeed_freq = val;
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		hs_util = target_util(sg_policy,
					sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	return count;
}

static ssize_t rtg_boost_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->rtg_boost_freq);
}

static ssize_t rtg_boost_freq_store(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	unsigned int val;
	struct sugov_policy *sg_policy;
	unsigned long boost_util;
	unsigned long flags;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	tunables->rtg_boost_freq = val;
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		boost_util = target_util(sg_policy,
					  sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	return count;
}

static ssize_t pl_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->pl);
}

static ssize_t pl_store(struct gov_attr_set *attr_set, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	if (kstrtobool(buf, &tunables->pl))
		return -EINVAL;

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);
static struct governor_attr hispeed_load = __ATTR_RW(hispeed_load);
static struct governor_attr hispeed_freq = __ATTR_RW(hispeed_freq);
static struct governor_attr rtg_boost_freq = __ATTR_RW(rtg_boost_freq);
static struct governor_attr pl = __ATTR_RW(pl);

static struct attribute *sugov_attrs[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&hispeed_load.attr,
	&hispeed_freq.attr,
	&rtg_boost_freq.attr,
	&pl.attr,
	NULL
};
ATTRIBUTE_GROUPS(sugov);

static void sugov_tunables_free(struct kobject *kobj)
{
	struct gov_attr_set *attr_set = container_of(kobj, struct gov_attr_set, kobj);

	kfree(to_sugov_tunables(attr_set));
}

static struct kobj_type sugov_tunables_ktype = {
	.default_groups = sugov_groups,
	.sysfs_ops = &governor_sysfs_ops,
	.release = &sugov_tunables_free,
};

static struct cpufreq_governor schedutil_gov;

static struct sugov_policy *sugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void sugov_policy_free(struct sugov_policy *sg_policy)
{
	kfree(sg_policy);
}

static int sugov_kthread_create(struct sugov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	if (policy->fast_switch_enabled)
		return 0;

	kthread_init_work(&sg_policy->work, sugov_work);
	kthread_init_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"sugov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create sugov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	init_irq_work(&sg_policy->irq_work, sugov_irq_work);
	mutex_init(&sg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void sugov_kthread_stop(struct sugov_policy *sg_policy)
{

	if (sg_policy->policy->fast_switch_enabled)
		return;

	kthread_flush_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
	mutex_destroy(&sg_policy->work_lock);
}

static struct sugov_tunables *sugov_tunables_alloc(struct sugov_policy *sg_policy)
{
	struct sugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &sg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void sugov_tunables_save(struct cpufreq_policy *policy,
		struct sugov_tunables *tunables)
{
	int cpu;
	struct sugov_tunables *cached = per_cpu(cached_tunables, policy->cpu);

	if (!have_governor_per_policy())
		return;

	if (!cached) {
		cached = kzalloc(sizeof(*tunables), GFP_KERNEL);
		if (!cached)
			return;

		for_each_cpu(cpu, policy->related_cpus)
			per_cpu(cached_tunables, cpu) = cached;
	}

	cached->pl = tunables->pl;
	cached->hispeed_load = tunables->hispeed_load;
	cached->rtg_boost_freq = tunables->rtg_boost_freq;
	cached->hispeed_freq = tunables->hispeed_freq;
	cached->up_rate_limit_us = tunables->up_rate_limit_us;
	cached->down_rate_limit_us = tunables->down_rate_limit_us;
}

static void sugov_clear_global_tunables(void)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;
}

static void sugov_tunables_restore(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	struct sugov_tunables *tunables = sg_policy->tunables;
	struct sugov_tunables *cached = per_cpu(cached_tunables, policy->cpu);

	if (!cached)
		return;

	tunables->pl = cached->pl;
	tunables->hispeed_load = cached->hispeed_load;
	tunables->rtg_boost_freq = cached->rtg_boost_freq;
	tunables->hispeed_freq = cached->hispeed_freq;
	tunables->up_rate_limit_us = cached->up_rate_limit_us;
	tunables->down_rate_limit_us = cached->down_rate_limit_us;
	sg_policy->up_rate_delay_ns = cached->up_rate_limit_us;
	sg_policy->down_rate_delay_ns = cached->down_rate_limit_us;
	update_min_rate_limit_ns(sg_policy);
}

static int sugov_init(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	unsigned long util;
	int ret = 0;

	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	ret = sugov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	tunables->up_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->down_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->hispeed_load = DEFAULT_HISPEED_LOAD;
	tunables->hispeed_freq = 0;

	switch (policy->cpu) {
	default:
	case 0:
		tunables->rtg_boost_freq = DEFAULT_CPU0_RTG_BOOST_FREQ;
		break;
	case 4:
		tunables->rtg_boost_freq = DEFAULT_CPU4_RTG_BOOST_FREQ;
		break;
	case 7:
		tunables->rtg_boost_freq = DEFAULT_CPU7_RTG_BOOST_FREQ;
		break;
	}

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	util = target_util(sg_policy, sg_policy->tunables->rtg_boost_freq);
	sg_policy->rtg_boost_util = util;

	stale_ns = sched_ravg_window + (sched_ravg_window >> 3);

	sugov_tunables_restore(policy);

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &sugov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   schedutil_gov.name);
	if (ret)
		goto fail;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	sugov_clear_global_tunables();

stop_kthread:
	sugov_kthread_stop(sg_policy);
	mutex_unlock(&global_tunables_lock);

free_sg_policy:
	sugov_policy_free(sg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static void sugov_exit(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	if (tunables->attr_set.usage_count == 1)
		sugov_tunables_save(policy, tunables);

	count = gov_attr_set_put(&tunables->attr_set, &sg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		sugov_clear_global_tunables();

	mutex_unlock(&global_tunables_lock);

	sugov_kthread_stop(sg_policy);
	sugov_policy_free(sg_policy);
	cpufreq_disable_fast_switch(policy);
}

static int sugov_start(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_ns(sg_policy);
	sg_policy->last_freq_update_time	= 0;
	sg_policy->next_freq			= 0;
	sg_policy->work_in_progress		= false;
	sg_policy->limits_changed		= false;
	sg_policy->need_freq_update		= false;
	sg_policy->cached_raw_freq		= 0;

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		memset(sg_cpu, 0, sizeof(*sg_cpu));
		sg_cpu->cpu			= cpu;
		sg_cpu->sg_policy		= sg_policy;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
					     policy_is_shared(policy) ?
							sugov_update_shared :
							sugov_update_single);
	}
	return 0;
}

static void sugov_stop(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_rcu();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&sg_policy->irq_work);
		kthread_cancel_work_sync(&sg_policy->work);
	}
}

static void sugov_limits(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned long flags, now;
	unsigned int freq;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		sugov_track_cycles(sg_policy, sg_policy->policy->cur,
				   ktime_get_ns());
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	} else {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		freq = policy->cur;
		now = ktime_get_ns();

		freq = cpufreq_driver_resolve_freq(policy, freq);
		sg_policy->cached_raw_freq = freq;
		sugov_fast_switch(sg_policy, now, freq);
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	smp_wmb();

	WRITE_ONCE(sg_policy->limits_changed, true);
}

static struct cpufreq_governor schedutil_gov = {
	.name			= "schedutil",
	.owner			= THIS_MODULE,
	.dynamic_switching	= true,
	.init			= sugov_init,
	.exit			= sugov_exit,
	.start			= sugov_start,
	.stop			= sugov_stop,
	.limits			= sugov_limits,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &schedutil_gov;
}
#endif

static int __init sugov_register(void)
{
	return cpufreq_register_governor(&schedutil_gov);
}
core_initcall(sugov_register);

unsigned long cpuidle_sched_cpu_util(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long max = arch_scale_cpu_capacity(cpu);

	return schedutil_cpu_util(cpu, cpu_util_cfs(rq), max, ENERGY_UTIL, NULL);
}
