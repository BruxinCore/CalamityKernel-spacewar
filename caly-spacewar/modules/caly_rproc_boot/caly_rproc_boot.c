#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/remoteproc.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/kmsg_dump.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>

static struct kobject *caly_rproc_kobj;
static struct work_struct caly_rproc_work;
static struct delayed_work caly_autoboot_work;
static struct delayed_work caly_wcn_rpm_work;
static int caly_wcn_rpm_tries;
static struct platform_device *caly_wcn_pdev;
static bool caly_wcn_rpm_held;
static DEFINE_MUTEX(caly_wcn_lock);

static void caly_wcn_apply_fix(void)
{
	struct device_node *np;
	int ret;

	mutex_lock(&caly_wcn_lock);
	if (!caly_wcn_pdev) {
		np = of_find_compatible_node(NULL, NULL, "qcom,wcn6750");
		if (np) {
			caly_wcn_pdev = of_find_device_by_node(np);
			of_node_put(np);
		}
	}
	if (caly_wcn_pdev) {
		if (!caly_wcn_rpm_held && !caly_wcn_pdev->dev.power.disable_depth) {
			ret = pm_runtime_get_sync(&caly_wcn_pdev->dev);
			if (ret < 0) {
				pm_runtime_put_noidle(&caly_wcn_pdev->dev);
			} else {
				caly_wcn_rpm_held = true;
				pr_info("caly_rproc: pinned WCN6750 runtime PM active\n");
			}
		}
		if (!caly_wcn_pdev->dev.power.disable_depth && caly_wcn_pdev->dev.power.runtime_auto) {
			pm_runtime_forbid(&caly_wcn_pdev->dev);
			pr_info("caly_rproc: disabled WCN6750 runtime PM (control=on)\n");
		}
	}
	mutex_unlock(&caly_wcn_lock);
}

static void caly_wcn_rpm_fn(struct work_struct *w)
{
	caly_wcn_apply_fix();
	if (caly_wcn_rpm_tries < 45) {
		caly_wcn_rpm_tries++;
		schedule_delayed_work(&caly_wcn_rpm_work, msecs_to_jiffies(2000));
	}
}

static int caly_wcn_netdev_cb(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);

	if ((event == NETDEV_REGISTER || event == NETDEV_UP) &&
	    ndev && !strncmp(ndev->name, "wlan", 4)) {
		if (caly_wcn_rpm_tries >= 35)
			caly_wcn_rpm_tries = 35;
		mod_delayed_work(system_wq, &caly_wcn_rpm_work, msecs_to_jiffies(500));
	}
	return NOTIFY_DONE;
}

static struct notifier_block caly_wcn_nb = {
	.notifier_call = caly_wcn_netdev_cb,
};

static char target_cmd[32];
static char last_status[128];
static bool adsp_done;
static bool cdsp_done;
static bool mss_done;
static int autoboot_tries;

static int caly_boot_one(const char *compat, const char *label)
{
	struct device_node *np;
	struct rproc *rp;
	int rc;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np) {
		snprintf(last_status, sizeof(last_status), "%s: node not found", label);
		return -ENODEV;
	}
	rp = rproc_get_by_phandle(np->phandle);
	of_node_put(np);
	if (!rp) {
		snprintf(last_status, sizeof(last_status), "%s: rproc not ready", label);
		return -EPROBE_DEFER;
	}
	if (rp->state == RPROC_RUNNING) {
		snprintf(last_status, sizeof(last_status), "%s: already running", label);
		return 0;
	}
	rc = rproc_boot(rp);
	snprintf(last_status, sizeof(last_status), "%s: rc=%d state=%d", label, rc, rp->state);
	pr_info("CALY_RPROC: %s\n", last_status);
	return rc;
}

static void caly_autoboot_fn(struct work_struct *work)
{
	int rc0 = 0, rc1 = 0, rc2 = 0;

	if (!adsp_done) {
		rc0 = caly_boot_one("qcom,yupik-adsp-pas", "adsp");
		if (rc0 == 0)
			adsp_done = true;
	}
	if (!cdsp_done) {
		rc1 = caly_boot_one("qcom,yupik-cdsp-pas", "cdsp");
		if (rc1 == 0)
			cdsp_done = true;
	}
	if (!mss_done) {
		rc2 = caly_boot_one("qcom,yupik-modem-pas", "mss");
		if (rc2 == 0)
			mss_done = true;
	}
	if ((!adsp_done || !cdsp_done || !mss_done) && autoboot_tries < 25) {
		autoboot_tries++;
		schedule_delayed_work(&caly_autoboot_work, msecs_to_jiffies(2000));
	}
}

static void caly_rproc_work_fn(struct work_struct *work)
{
	if (!strcmp(target_cmd, "adsp"))
		caly_boot_one("qcom,yupik-adsp-pas", "adsp");
	else if (!strcmp(target_cmd, "cdsp"))
		caly_boot_one("qcom,yupik-cdsp-pas", "cdsp");
	else if (!strcmp(target_cmd, "mss"))
		caly_boot_one("qcom,yupik-modem-pas", "mss");
	else if (!strcmp(target_cmd, "wpss"))
		caly_boot_one("qcom,yupik-wpss-pas", "wpss");
	else
		snprintf(last_status, sizeof(last_status), "unknown: %s", target_cmd);
}

static ssize_t boot_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	size_t n = min_t(size_t, count, sizeof(target_cmd) - 1);

	memcpy(target_cmd, buf, n);
	target_cmd[n] = '\0';
	strim(target_cmd);
	schedule_work(&caly_rproc_work);
	return count;
}

static ssize_t boot_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", last_status);
}

static struct kobj_attribute boot_attr = {
	.attr = { .name = "boot", .mode = 0666 },
	.show = boot_show,
	.store = boot_store,
};

#define KMSG_SNAP_MAX 524288
static char *kmsg_snap;
static size_t kmsg_snap_len;
static DEFINE_MUTEX(kmsg_lock);

static ssize_t kmsg_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf,
			 loff_t off, size_t count)
{
	size_t avail;

	mutex_lock(&kmsg_lock);
	if (off == 0 || !kmsg_snap) {
		struct kmsg_dump_iter iter;
		size_t total = 0;
		size_t line_len = 0;
		char line[1024];

		if (!kmsg_snap)
			kmsg_snap = vmalloc(KMSG_SNAP_MAX);
		if (!kmsg_snap) {
			mutex_unlock(&kmsg_lock);
			return -ENOMEM;
		}
		kmsg_dump_rewind(&iter);
		while (kmsg_dump_get_line(&iter, true, line, sizeof(line) - 1, &line_len)) {
			line[line_len] = '\0';
			if (strstr(line, "avc:") || strstr(line, "Skipped access check") ||
			    strstr(line, "CALY_DRM:"))
				continue;
			if (total + line_len >= KMSG_SNAP_MAX - 1) {
				memmove(kmsg_snap, kmsg_snap + (KMSG_SNAP_MAX / 2), total - (KMSG_SNAP_MAX / 2));
				total -= (KMSG_SNAP_MAX / 2);
			}
			memcpy(kmsg_snap + total, line, line_len);
			total += line_len;
		}
		kmsg_snap_len = total;
	}
	if (off >= kmsg_snap_len) {
		mutex_unlock(&kmsg_lock);
		return 0;
	}
	avail = min_t(size_t, count, kmsg_snap_len - off);
	memcpy(buf, kmsg_snap + off, avail);
	mutex_unlock(&kmsg_lock);
	return avail;
}

static struct bin_attribute kmsg_attr = {
	.attr = { .name = "kmsg", .mode = 0444 },
	.size = KMSG_SNAP_MAX,
	.read = kmsg_read,
};

static int __init caly_rproc_boot_init(void)
{
	int rc;

	INIT_WORK(&caly_rproc_work, caly_rproc_work_fn);
	INIT_DELAYED_WORK(&caly_autoboot_work, caly_autoboot_fn);
	INIT_DELAYED_WORK(&caly_wcn_rpm_work, caly_wcn_rpm_fn);
	snprintf(last_status, sizeof(last_status), "ready");
	caly_rproc_kobj = kobject_create_and_add("caly_rproc", kernel_kobj);
	if (!caly_rproc_kobj)
		return -ENOMEM;
	rc = sysfs_create_file(caly_rproc_kobj, &boot_attr.attr);
	if (rc) {
		kobject_put(caly_rproc_kobj);
		return rc;
	}
	rc = sysfs_create_bin_file(caly_rproc_kobj, &kmsg_attr);
	if (rc) {
		sysfs_remove_file(caly_rproc_kobj, &boot_attr.attr);
		kobject_put(caly_rproc_kobj);
		return rc;
	}
	register_netdevice_notifier(&caly_wcn_nb);
	schedule_delayed_work(&caly_autoboot_work, msecs_to_jiffies(4000));
	schedule_delayed_work(&caly_wcn_rpm_work, msecs_to_jiffies(5000));
	return 0;
}

static void __exit caly_rproc_boot_exit(void)
{
	unregister_netdevice_notifier(&caly_wcn_nb);
	cancel_delayed_work_sync(&caly_wcn_rpm_work);
	cancel_delayed_work_sync(&caly_autoboot_work);
	cancel_work_sync(&caly_rproc_work);
	mutex_lock(&caly_wcn_lock);
	if (caly_wcn_pdev) {
		if (caly_wcn_rpm_held) {
			pm_runtime_put_noidle(&caly_wcn_pdev->dev);
			caly_wcn_rpm_held = false;
		}
		platform_device_put(caly_wcn_pdev);
		caly_wcn_pdev = NULL;
	}
	mutex_unlock(&caly_wcn_lock);
	if (caly_rproc_kobj) {
		sysfs_remove_bin_file(caly_rproc_kobj, &kmsg_attr);
		sysfs_remove_file(caly_rproc_kobj, &boot_attr.attr);
		kobject_put(caly_rproc_kobj);
	}
	vfree(kmsg_snap);
}

module_init(caly_rproc_boot_init);
module_exit(caly_rproc_boot_exit);
MODULE_LICENSE("GPL");
