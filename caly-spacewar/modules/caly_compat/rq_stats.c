#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/math64.h>

static struct kobject *rq_kobj;
static struct delayed_work def_dwork;
static s64 def_start_ns;

static void def_work_fn(struct work_struct *work)
{
	if (rq_kobj)
		sysfs_notify(rq_kobj, NULL, "def_timer_ms");
}

static ssize_t def_timer_ms_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	s64 diff = ktime_to_ns(ktime_get()) - def_start_ns;
	unsigned int udiff;

	if (diff < 0)
		diff = 0;
	udiff = (unsigned int)div_u64((u64)diff, 1000000ULL);
	return sysfs_emit(buf, "%u\n", udiff);
}

static ssize_t def_timer_ms_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	def_start_ns = ktime_to_ns(ktime_get());
	if (val > 0)
		mod_delayed_work(system_wq, &def_dwork, msecs_to_jiffies(val));
	else
		cancel_delayed_work(&def_dwork);
	return count;
}

static struct kobj_attribute def_timer_ms_attr =
	__ATTR(def_timer_ms, 0600, def_timer_ms_show, def_timer_ms_store);

static struct attribute *rq_attrs[] = {
	&def_timer_ms_attr.attr,
	NULL,
};

static const struct attribute_group rq_attr_group = {
	.attrs = rq_attrs,
};

static int __init caly_rq_stats_init(void)
{
	struct device *cpu_dev = get_cpu_device(0);
	int ret;

	if (!cpu_dev)
		return -ENODEV;

	INIT_DEFERRABLE_WORK(&def_dwork, def_work_fn);
	def_start_ns = ktime_to_ns(ktime_get());

	rq_kobj = kobject_create_and_add("rq-stats", &cpu_dev->kobj);
	if (!rq_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(rq_kobj, &rq_attr_group);
	if (ret) {
		kobject_put(rq_kobj);
		rq_kobj = NULL;
		return ret;
	}
	return 0;
}

static void __exit caly_rq_stats_exit(void)
{
	cancel_delayed_work_sync(&def_dwork);
	if (rq_kobj) {
		sysfs_remove_group(rq_kobj, &rq_attr_group);
		kobject_put(rq_kobj);
		rq_kobj = NULL;
	}
}

module_init(caly_rq_stats_init);
module_exit(caly_rq_stats_exit);
MODULE_LICENSE("GPL");
