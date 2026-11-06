#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/rwsem.h>
#include <linux/zsmalloc.h>
#include <linux/crypto.h>

#include "zcomp.h"

#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))

#define ZRAM_FLAG_SHIFT (PAGE_SHIFT + 1)

enum zram_pageflags {

	ZRAM_LOCK = ZRAM_FLAG_SHIFT,
	ZRAM_SAME,
	ZRAM_WB,
	ZRAM_UNDER_WB,
	ZRAM_HUGE,
	ZRAM_IDLE,

	__NR_ZRAM_PAGEFLAGS,
};

struct zram_table_entry {
	union {
		unsigned long handle;
		unsigned long element;
	};
	unsigned long flags;
#ifdef CONFIG_ZRAM_MEMORY_TRACKING
	ktime_t ac_time;
#endif
};

struct zram_stats {
	atomic64_t compr_data_size;
	atomic64_t num_reads;
	atomic64_t num_writes;
	atomic64_t failed_reads;
	atomic64_t failed_writes;
	atomic64_t invalid_io;
	atomic64_t notify_free;
	atomic64_t same_pages;
	atomic64_t huge_pages;
	atomic64_t pages_stored;
	atomic_long_t max_used_pages;
	atomic64_t writestall;
	atomic64_t miss_free;
#ifdef	CONFIG_ZRAM_WRITEBACK
	atomic64_t bd_count;
	atomic64_t bd_reads;
	atomic64_t bd_writes;
#endif
};

struct zram {
	struct zram_table_entry *table;
	struct zs_pool *mem_pool;
	struct zcomp *comp;
	struct gendisk *disk;

	struct rw_semaphore init_lock;

	unsigned long limit_pages;

	struct zram_stats stats;

	u64 disksize;
	char compressor[CRYPTO_MAX_ALG_NAME];

	bool claim;
	struct file *backing_dev;
#ifdef CONFIG_ZRAM_WRITEBACK
	spinlock_t wb_limit_lock;
	bool wb_limit_enable;
	u64 bd_wb_limit;
	struct block_device *bdev;
	unsigned int old_block_size;
	unsigned long *bitmap;
	unsigned long nr_pages;
#endif
#ifdef CONFIG_ZRAM_MEMORY_TRACKING
	struct dentry *debugfs_dir;
#endif
};
#endif
