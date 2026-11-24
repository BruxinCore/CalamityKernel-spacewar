#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/string.h>

static int enable_ramdumps;
module_param(enable_ramdumps, int, 0644);

static int enable_mini_ramdumps;
module_param(enable_mini_ramdumps, int, 0644);

static const char *const subsys_names[] = {
	"modem",
	"adsp",
	"cdsp",
	"wpss",
	"venus",
};

#define NUM_SUBSYS ARRAY_SIZE(subsys_names)

struct caly_subsys_dev {
	struct device dev;
	const char *name;
	char restart_level[16];
	int id;
	struct cdev cdev;
};

static struct caly_subsys_dev *subsys_devs[NUM_SUBSYS];
static struct class *subsys_char_class;
static dev_t subsys_devt;

static ssize_t name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct caly_subsys_dev *s = container_of(dev, struct caly_subsys_dev, dev);

	return sysfs_emit(buf, "%s\n", s->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "ONLINE\n");
}
static DEVICE_ATTR_RO(state);

static ssize_t crash_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}
static DEVICE_ATTR_RO(crash_count);

static ssize_t firmware_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct caly_subsys_dev *s = container_of(dev, struct caly_subsys_dev, dev);

	return sysfs_emit(buf, "%s\n", s->name);
}
static DEVICE_ATTR_RO(firmware_name);

static ssize_t restart_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct caly_subsys_dev *s = container_of(dev, struct caly_subsys_dev, dev);

	return sysfs_emit(buf, "%s\n", s->restart_level);
}

static ssize_t restart_level_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct caly_subsys_dev *s = container_of(dev, struct caly_subsys_dev, dev);

	if (sysfs_streq(buf, "RELATED") || sysfs_streq(buf, "related"))
		strscpy(s->restart_level, "RELATED", sizeof(s->restart_level));
	else if (sysfs_streq(buf, "SYSTEM") || sysfs_streq(buf, "system"))
		strscpy(s->restart_level, "SYSTEM", sizeof(s->restart_level));
	return count;
}
static DEVICE_ATTR_RW(restart_level);

static struct attribute *caly_subsys_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_crash_count.attr,
	&dev_attr_restart_level.attr,
	&dev_attr_firmware_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(caly_subsys);

static const struct bus_type caly_msm_subsys_bus = {
	.name = "msm_subsys",
	.dev_groups = caly_subsys_groups,
};

static void caly_subsys_release(struct device *dev)
{
	struct caly_subsys_dev *s = container_of(dev, struct caly_subsys_dev, dev);

	kfree(s);
}

static int caly_subsys_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int caly_subsys_release_fop(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations caly_subsys_fops = {
	.owner = THIS_MODULE,
	.open = caly_subsys_open,
	.release = caly_subsys_release_fop,
};

static int __init caly_ssr_param_init(void)
{
	int ret, i;

	ret = bus_register(&caly_msm_subsys_bus);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&subsys_devt, 0, NUM_SUBSYS, "subsys");
	if (ret)
		goto err_bus;

	subsys_char_class = class_create("subsys");
	if (IS_ERR(subsys_char_class)) {
		ret = PTR_ERR(subsys_char_class);
		goto err_chrdev;
	}

	for (i = 0; i < NUM_SUBSYS; i++) {
		struct caly_subsys_dev *s = kzalloc(sizeof(*s), GFP_KERNEL);
		dev_t d = MKDEV(MAJOR(subsys_devt), i);

		if (!s)
			continue;
		s->id = i;
		s->name = subsys_names[i];
		strscpy(s->restart_level, "RELATED", sizeof(s->restart_level));
		s->dev.bus = &caly_msm_subsys_bus;
		s->dev.release = caly_subsys_release;
		dev_set_name(&s->dev, "subsys%d", i);
		if (device_register(&s->dev)) {
			put_device(&s->dev);
			continue;
		}
		cdev_init(&s->cdev, &caly_subsys_fops);
		s->cdev.owner = THIS_MODULE;
		if (cdev_add(&s->cdev, d, 1) == 0)
			device_create(subsys_char_class, NULL, d, NULL, "subsys_%s", s->name);
		subsys_devs[i] = s;
	}

	return 0;

err_chrdev:
	unregister_chrdev_region(subsys_devt, NUM_SUBSYS);
err_bus:
	bus_unregister(&caly_msm_subsys_bus);
	return ret;
}

static void __exit caly_ssr_param_exit(void)
{
	int i;

	for (i = 0; i < NUM_SUBSYS; i++) {
		if (!subsys_devs[i])
			continue;
		device_destroy(subsys_char_class, MKDEV(MAJOR(subsys_devt), i));
		cdev_del(&subsys_devs[i]->cdev);
		device_unregister(&subsys_devs[i]->dev);
		subsys_devs[i] = NULL;
	}
	class_destroy(subsys_char_class);
	unregister_chrdev_region(subsys_devt, NUM_SUBSYS);
	bus_unregister(&caly_msm_subsys_bus);
}

module_init(caly_ssr_param_init);
module_exit(caly_ssr_param_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("NerokiStyle (BruxinCore)");
MODULE_DESCRIPTION("CALY-Daitaku Spacewar legacy subsystem_restart compatibility driver");
