#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/shmem_fs.h>
#include <linux/personality.h>
#include <linux/compat.h>
#include <linux/ioctl.h>

#define ASHMEM_NAME_LEN 256
#define ASHMEM_NAME_PREFIX "dev/ashmem/"
#define ASHMEM_NAME_PREFIX_LEN (sizeof(ASHMEM_NAME_PREFIX) - 1)
#define ASHMEM_FULL_NAME_LEN (ASHMEM_NAME_LEN + ASHMEM_NAME_PREFIX_LEN)
#define ASHMEM_NAME_DEF "dev/ashmem"

#define ASHMEM_NOT_PURGED 0
#define ASHMEM_IS_PINNED 1

struct caly_ashmem_pin {
	__u32 offset;
	__u32 len;
};

#define __ASHMEMIOC 0x77
#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE _IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN _IOW(__ASHMEMIOC, 7, struct caly_ashmem_pin)
#define ASHMEM_UNPIN _IOW(__ASHMEMIOC, 8, struct caly_ashmem_pin)
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)
#define COMPAT_ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, compat_size_t)
#define COMPAT_ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned int)

#define PROT_MASK (PROT_EXEC | PROT_READ | PROT_WRITE)

struct caly_ashmem_area {
	struct mutex lock;
	char name[ASHMEM_FULL_NAME_LEN];
	struct file *file;
	size_t size;
	unsigned long prot_mask;
};

static inline unsigned long caly_calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ, VM_MAYREAD) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC, VM_MAYEXEC);
}

static int caly_ashmem_open(struct inode *inode, struct file *file)
{
	struct caly_ashmem_area *asma;

	asma = kzalloc(sizeof(*asma), GFP_KERNEL);
	if (!asma)
		return -ENOMEM;

	mutex_init(&asma->lock);
	memcpy(asma->name, ASHMEM_NAME_PREFIX, ASHMEM_NAME_PREFIX_LEN);
	asma->prot_mask = PROT_MASK;
	file->private_data = asma;
	return 0;
}

static int caly_ashmem_release(struct inode *inode, struct file *file)
{
	struct caly_ashmem_area *asma = file->private_data;

	if (asma) {
		if (asma->file)
			fput(asma->file);
		kfree(asma);
	}
	return 0;
}

static ssize_t caly_ashmem_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct caly_ashmem_area *asma = iocb->ki_filp->private_data;
	struct file *vmfile;
	ssize_t ret;

	mutex_lock(&asma->lock);
	if (!asma->file) {
		mutex_unlock(&asma->lock);
		return 0;
	}
	vmfile = get_file(asma->file);
	mutex_unlock(&asma->lock);

	ret = vfs_iter_read(vmfile, iter, &iocb->ki_pos, 0);
	fput(vmfile);
	return ret;
}

static loff_t caly_ashmem_llseek(struct file *file, loff_t offset, int origin)
{
	struct caly_ashmem_area *asma = file->private_data;
	struct file *vmfile;
	loff_t ret;

	mutex_lock(&asma->lock);
	if (!asma->size || !asma->file) {
		mutex_unlock(&asma->lock);
		return -EINVAL;
	}
	vmfile = get_file(asma->file);
	mutex_unlock(&asma->lock);

	ret = vfs_llseek(vmfile, offset, origin);
	if (ret >= 0)
		file->f_pos = vmfile->f_pos;
	fput(vmfile);
	return ret;
}

static int caly_ashmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct caly_ashmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&asma->lock);
	if (!asma->size) {
		ret = -EINVAL;
		goto out;
	}
	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(asma->size)) {
		ret = -EINVAL;
		goto out;
	}
	if ((vma->vm_flags & ~calc_vm_prot_bits(asma->prot_mask, 0)) &
	    calc_vm_prot_bits(PROT_MASK, 0)) {
		ret = -EPERM;
		goto out;
	}
	vm_flags_clear(vma, caly_calc_vm_may_flags(~asma->prot_mask));

	if (!asma->file) {
		const char *name = ASHMEM_NAME_DEF;
		struct file *vmfile;

		if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0')
			name = asma->name;

		vmfile = shmem_file_setup(name, asma->size, vma->vm_flags);
		if (IS_ERR(vmfile)) {
			ret = PTR_ERR(vmfile);
			goto out;
		}
		vmfile->f_mode |= FMODE_LSEEK;
		asma->file = vmfile;
	}

	vma_set_file(vma, asma->file);
	ret = call_mmap(asma->file, vma);
out:
	mutex_unlock(&asma->lock);
	return ret;
}

static long caly_ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct caly_ashmem_area *asma = file->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case ASHMEM_SET_NAME: {
		char local_name[ASHMEM_NAME_LEN];
		long len = strncpy_from_user(local_name, (void __user *)arg, ASHMEM_NAME_LEN);
		if (len < 0)
			return len;
		if (len == ASHMEM_NAME_LEN)
			local_name[ASHMEM_NAME_LEN - 1] = '\0';
		mutex_lock(&asma->lock);
		if (asma->file)
			ret = -EINVAL;
		else {
			strcpy(asma->name + ASHMEM_NAME_PREFIX_LEN, local_name);
			ret = 0;
		}
		mutex_unlock(&asma->lock);
		break;
	}
	case ASHMEM_GET_NAME: {
		char local_name[ASHMEM_NAME_LEN];
		size_t len;
		mutex_lock(&asma->lock);
		if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0') {
			len = strlen(asma->name + ASHMEM_NAME_PREFIX_LEN) + 1;
			memcpy(local_name, asma->name + ASHMEM_NAME_PREFIX_LEN, len);
		} else {
			len = sizeof(ASHMEM_NAME_DEF);
			memcpy(local_name, ASHMEM_NAME_DEF, len);
		}
		mutex_unlock(&asma->lock);
		ret = copy_to_user((void __user *)arg, local_name, len) ? -EFAULT : 0;
		break;
	}
	case ASHMEM_SET_SIZE:
		mutex_lock(&asma->lock);
		if (!asma->file) {
			asma->size = (size_t)arg;
			ret = 0;
		} else {
			ret = -EINVAL;
		}
		mutex_unlock(&asma->lock);
		break;
	case ASHMEM_GET_SIZE:
		ret = asma->size;
		break;
	case ASHMEM_SET_PROT_MASK:
		mutex_lock(&asma->lock);
		if ((asma->prot_mask & arg) != arg) {
			ret = -EINVAL;
		} else {
			unsigned long prot = arg;
			if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
				prot |= PROT_EXEC;
			asma->prot_mask = prot;
			ret = 0;
		}
		mutex_unlock(&asma->lock);
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = asma->prot_mask;
		break;
	case ASHMEM_PIN:
	case ASHMEM_UNPIN:
	case ASHMEM_PURGE_ALL_CACHES:
		ret = ASHMEM_NOT_PURGED;
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ASHMEM_IS_PINNED;
		break;
	}
	return ret;
}

static long caly_ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case COMPAT_ASHMEM_SET_SIZE:
		cmd = ASHMEM_SET_SIZE;
		break;
	case COMPAT_ASHMEM_SET_PROT_MASK:
		cmd = ASHMEM_SET_PROT_MASK;
		break;
	}
	return caly_ashmem_ioctl(file, cmd, arg);
}

static const struct file_operations caly_ashmem_fops = {
	.owner = THIS_MODULE,
	.open = caly_ashmem_open,
	.release = caly_ashmem_release,
	.read_iter = caly_ashmem_read_iter,
	.llseek = caly_ashmem_llseek,
	.mmap = caly_ashmem_mmap,
	.unlocked_ioctl = caly_ashmem_ioctl,
	.compat_ioctl = caly_ashmem_compat_ioctl,
};

static struct miscdevice caly_ashmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &caly_ashmem_fops,
	.mode = 0666,
};

static int __init caly_ashmem_init(void)
{
	int ret = misc_register(&caly_ashmem_misc);
	pr_emerg("CALY_ASHMEM: registered /dev/ashmem ret=%d\n", ret);
	return ret;
}

static void __exit caly_ashmem_exit(void)
{
	misc_deregister(&caly_ashmem_misc);
}

module_init(caly_ashmem_init);
module_exit(caly_ashmem_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("NerokiStyle (BruxinCore)");
MODULE_DESCRIPTION("CALY-Daitaku Spacewar Linux 6.12 ashmem compatibility driver");
