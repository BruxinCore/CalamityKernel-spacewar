#ifndef __EROFS_INTERNAL_H
#define __EROFS_INTERNAL_H

#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "erofs_fs.h"

#undef pr_fmt
#define pr_fmt(fmt) "erofs: " fmt

__printf(3, 4) void _erofs_err(struct super_block *sb,
			       const char *function, const char *fmt, ...);
#define erofs_err(sb, fmt, ...)	\
	_erofs_err(sb, __func__, fmt "\n", ##__VA_ARGS__)
__printf(3, 4) void _erofs_info(struct super_block *sb,
			       const char *function, const char *fmt, ...);
#define erofs_info(sb, fmt, ...) \
	_erofs_info(sb, __func__, fmt "\n", ##__VA_ARGS__)
#ifdef CONFIG_EROFS_FS_DEBUG
#define erofs_dbg(x, ...)       pr_debug(x "\n", ##__VA_ARGS__)
#define DBG_BUGON               BUG_ON
#else
#define erofs_dbg(x, ...)       ((void)0)
#define DBG_BUGON(x)            ((void)(x))
#endif

#define EROFS_SUPER_MAGIC   EROFS_SUPER_MAGIC_V1

typedef u64 erofs_nid_t;
typedef u64 erofs_off_t;

typedef u32 erofs_blk_t;

struct erofs_sb_info {
#ifdef CONFIG_EROFS_FS_ZIP

	struct list_head list;
	struct mutex umount_mutex;

	struct radix_tree_root workstn_tree;

	unsigned int max_sync_decompress_pages;
	struct {
		u16 max_distance_pages;
		u16 max_pclusterblks;
	} lz4;

	unsigned int shrinker_run_no;

	unsigned char cache_strategy;

	struct inode *managed_cache;
#endif
	u32 blocks;
	u32 meta_blkaddr;
#ifdef CONFIG_EROFS_FS_XATTR
	u32 xattr_blkaddr;
#endif

	unsigned char islotbits;

	u32 build_time_nsec;
	u64 build_time;

	erofs_nid_t root_nid;

	u64 inos;

	u8 uuid[16];
	u8 volume_name[16];
	u32 feature_incompat;

	unsigned int mount_opt;
};

#define EROFS_SB(sb) ((struct erofs_sb_info *)(sb)->s_fs_info)
#define EROFS_I_SB(inode) ((struct erofs_sb_info *)(inode)->i_sb->s_fs_info)

#define EROFS_MOUNT_XATTR_USER		0x00000010
#define EROFS_MOUNT_POSIX_ACL		0x00000020

#define clear_opt(sbi, option)	((sbi)->mount_opt &= ~EROFS_MOUNT_##option)
#define set_opt(sbi, option)	((sbi)->mount_opt |= EROFS_MOUNT_##option)
#define test_opt(sbi, option)	((sbi)->mount_opt & EROFS_MOUNT_##option)

#ifdef CONFIG_EROFS_FS_ZIP
enum {
	EROFS_ZIP_CACHE_DISABLED,
	EROFS_ZIP_CACHE_READAHEAD,
	EROFS_ZIP_CACHE_READAROUND
};

#define EROFS_LOCKED_MAGIC     (INT_MIN | 0xE0F510CCL)

struct erofs_workgroup {

	pgoff_t index;

	atomic_t refcount;
};

#if defined(CONFIG_SMP)
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();
	if (val != atomic_cmpxchg(&grp->refcount, val, EROFS_LOCKED_MAGIC)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{

	smp_mb();
	atomic_set(&grp->refcount, orig_val);
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	return atomic_cond_read_relaxed(&grp->refcount,
					VAL != EROFS_LOCKED_MAGIC);
}
#else
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();

	if (val != atomic_read(&grp->refcount)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	int v = atomic_read(&grp->refcount);

	DBG_BUGON(v == EROFS_LOCKED_MAGIC);
	return v;
}
#endif

#define Z_EROFS_CLUSTER_MAX_PAGES       (CONFIG_EROFS_FS_CLUSTER_PAGE_LIMIT)
#endif

#define LOG_BLOCK_SIZE		PAGE_SHIFT

#undef LOG_SECTORS_PER_BLOCK
#define LOG_SECTORS_PER_BLOCK	(PAGE_SHIFT - 9)

#undef SECTORS_PER_BLOCK
#define SECTORS_PER_BLOCK	(1 << SECTORS_PER_BLOCK)

#define EROFS_BLKSIZ		(1 << LOG_BLOCK_SIZE)

#if (EROFS_BLKSIZ % 4096 || !EROFS_BLKSIZ)
#error erofs cannot be used in this platform
#endif

#define ROOT_NID(sb)		((sb)->root_nid)

#define erofs_blknr(addr)       ((addr) / EROFS_BLKSIZ)
#define erofs_blkoff(addr)      ((addr) % EROFS_BLKSIZ)
#define blknr_to_addr(nr)       ((erofs_off_t)(nr) * EROFS_BLKSIZ)

static inline erofs_off_t iloc(struct erofs_sb_info *sbi, erofs_nid_t nid)
{
	return blknr_to_addr(sbi->meta_blkaddr) + (nid << sbi->islotbits);
}

#define EROFS_I_EA_INITED_BIT	0
#define EROFS_I_Z_INITED_BIT	1

#define EROFS_I_BL_XATTR_BIT	(BITS_PER_LONG - 1)
#define EROFS_I_BL_Z_BIT	(BITS_PER_LONG - 2)

struct erofs_inode {
	erofs_nid_t nid;

	unsigned long flags;

	unsigned char datalayout;
	unsigned char inode_isize;
	unsigned int xattr_isize;

	unsigned int xattr_shared_count;
	unsigned int *xattr_shared_xattrs;

	union {
		erofs_blk_t raw_blkaddr;
#ifdef CONFIG_EROFS_FS_ZIP
		struct {
			unsigned short z_advise;
			unsigned char  z_algorithmtype[2];
			unsigned char  z_logical_clusterbits;
			unsigned char  z_physical_clusterbits[2];
		};
#endif
	};

	struct inode vfs_inode;
};

#define EROFS_I(ptr)	\
	container_of(ptr, struct erofs_inode, vfs_inode)

static inline unsigned long erofs_inode_datablocks(struct inode *inode)
{

	return DIV_ROUND_UP(inode->i_size, EROFS_BLKSIZ);
}

static inline unsigned int erofs_bitrange(unsigned int value, unsigned int bit,
					  unsigned int bits)
{

	return (value >> bit) & ((1 << bits) - 1);
}

static inline unsigned int erofs_inode_version(unsigned int value)
{
	return erofs_bitrange(value, EROFS_I_VERSION_BIT,
			      EROFS_I_VERSION_BITS);
}

static inline unsigned int erofs_inode_datalayout(unsigned int value)
{
	return erofs_bitrange(value, EROFS_I_DATALAYOUT_BIT,
			      EROFS_I_DATALAYOUT_BITS);
}

extern const struct super_operations erofs_sops;

extern const struct address_space_operations erofs_raw_access_aops;
#ifdef CONFIG_EROFS_FS_ZIP
extern const struct address_space_operations z_erofs_vle_normalaccess_aops;
#endif

enum {
	BH_Zipped = BH_PrivateStart,
	BH_FullMapped,
};

#define EROFS_MAP_MAPPED	(1 << BH_Mapped)

#define EROFS_MAP_META		(1 << BH_Meta)

#define EROFS_MAP_ZIPPED	(1 << BH_Zipped)

#define EROFS_MAP_FULL_MAPPED	(1 << BH_FullMapped)

struct erofs_map_blocks {
	erofs_off_t m_pa, m_la;
	u64 m_plen, m_llen;

	unsigned int m_flags;

	struct page *mpage;
};

#define EROFS_GET_BLOCKS_RAW    0x0001

#ifdef CONFIG_EROFS_FS_ZIP
int z_erofs_fill_inode(struct inode *inode);
int z_erofs_map_blocks_iter(struct inode *inode,
			    struct erofs_map_blocks *map,
			    int flags);
#else
static inline int z_erofs_fill_inode(struct inode *inode) { return -EOPNOTSUPP; }
static inline int z_erofs_map_blocks_iter(struct inode *inode,
					  struct erofs_map_blocks *map,
					  int flags)
{
	return -EOPNOTSUPP;
}
#endif

struct page *erofs_get_meta_page(struct super_block *sb, erofs_blk_t blkaddr);

int erofs_map_blocks(struct inode *, struct erofs_map_blocks *, int);

static inline unsigned long erofs_inode_hash(erofs_nid_t nid)
{
#if BITS_PER_LONG == 32
	return (nid >> 32) ^ (nid & 0xffffffff);
#else
	return nid;
#endif
}

extern const struct inode_operations erofs_generic_iops;
extern const struct inode_operations erofs_symlink_iops;
extern const struct inode_operations erofs_fast_symlink_iops;

struct inode *erofs_iget(struct super_block *sb, erofs_nid_t nid, bool dir);
int erofs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int query_flags);

extern const struct inode_operations erofs_dir_iops;

int erofs_namei(struct inode *dir, struct qstr *name,
		erofs_nid_t *nid, unsigned int *d_type);

extern const struct file_operations erofs_dir_fops;

struct page *erofs_allocpage(struct list_head *pool, gfp_t gfp, bool nofail);

static inline bool erofs_sb_has_lz4_0padding(struct erofs_sb_info *sbi)
{
	return sbi->feature_incompat & EROFS_FEATURE_INCOMPAT_LZ4_0PADDING;
}

static inline void *erofs_vm_map_ram(struct page **pages, unsigned int count)
{
	int retried = 0;

	while (1) {
		void *p = vm_map_ram(pages, count, -1, PAGE_KERNEL);

		if (p || ++retried >= 3)
			return p;
		vm_unmap_aliases();
	}
	return NULL;
}

void *erofs_get_pcpubuf(unsigned int requiredpages);
void erofs_put_pcpubuf(void *ptr);
int erofs_pcpubuf_growsize(unsigned int nrpages);
void erofs_pcpubuf_init(void);
void erofs_pcpubuf_exit(void);

#ifdef CONFIG_EROFS_FS_ZIP
int erofs_workgroup_put(struct erofs_workgroup *grp);
struct erofs_workgroup *erofs_find_workgroup(struct super_block *sb,
					     pgoff_t index, bool *tag);
int erofs_register_workgroup(struct super_block *sb,
			     struct erofs_workgroup *grp, bool tag);
void erofs_workgroup_free_rcu(struct erofs_workgroup *grp);
void erofs_shrinker_register(struct super_block *sb);
void erofs_shrinker_unregister(struct super_block *sb);
int __init erofs_init_shrinker(void);
void erofs_exit_shrinker(void);
int z_erofs_load_lz4_config(struct super_block *sb, u16 distance);
int __init z_erofs_init_zip_subsystem(void);
void z_erofs_exit_zip_subsystem(void);
int erofs_try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
				       struct erofs_workgroup *egrp);
int erofs_try_to_free_cached_page(struct address_space *mapping,
				  struct page *page);
#else
static inline void erofs_shrinker_register(struct super_block *sb) {}
static inline void erofs_shrinker_unregister(struct super_block *sb) {}
static inline int erofs_init_shrinker(void) { return 0; }
static inline void erofs_exit_shrinker(void) {}
static inline int z_erofs_load_lz4_config(struct super_block *sb, u16 distance) { return 0; }
static inline int z_erofs_init_zip_subsystem(void) { return 0; }
static inline void z_erofs_exit_zip_subsystem(void) {}
#endif

#define EFSCORRUPTED    EUCLEAN

#endif
