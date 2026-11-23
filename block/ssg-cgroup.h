#ifndef _SSG_CGROUP_H
#define _SSG_CGROUP_H

#include <linux/blk-cgroup.h>

static inline int ssg_blkcg_init(void) { return 0; }
static inline void ssg_blkcg_exit(void) {}
static inline void ssg_blkcg_activate(struct request_queue *q) {}
static inline void ssg_blkcg_deactivate(struct request_queue *q) {}
static inline void ssg_blkcg_inc_rq(struct blkcg_gq *blkg) {}
static inline void ssg_blkcg_dec_rq(struct blkcg_gq *blkg) {}
static inline void ssg_blkcg_depth_updated(struct blk_mq_hw_ctx *hctx) {}
static inline unsigned int ssg_blkcg_shallow_depth(struct request_queue *q) { return 0; }

#endif
