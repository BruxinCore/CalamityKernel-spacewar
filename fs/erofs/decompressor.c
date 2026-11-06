#include "compress.h"
#include <linux/module.h>
#include <linux/lz4.h>

#ifndef LZ4_DISTANCE_MAX
#define LZ4_DISTANCE_MAX 65535
#endif

#define LZ4_MAX_DISTANCE_PAGES	(DIV_ROUND_UP(LZ4_DISTANCE_MAX, PAGE_SIZE) + 1)
#ifndef LZ4_DECOMPRESS_INPLACE_MARGIN
#define LZ4_DECOMPRESS_INPLACE_MARGIN(srcsize)  (((srcsize) >> 8) + 32)
#endif

int z_erofs_load_lz4_config(struct super_block *sb, u16 distance)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	sbi->lz4.max_pclusterblks = 1;
	sbi->lz4.max_distance_pages = distance ?
		min_t(unsigned int, DIV_ROUND_UP(distance, PAGE_SIZE) + 1,
		      LZ4_MAX_DISTANCE_PAGES) :
		LZ4_MAX_DISTANCE_PAGES;
	return erofs_pcpubuf_growsize(sbi->lz4.max_pclusterblks);
}

static int z_erofs_lz4_prepare_dstpages(struct z_erofs_decompress_req *rq,
					struct list_head *pagepool)
{
	const unsigned int nr =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	struct page *availables[LZ4_MAX_DISTANCE_PAGES] = { NULL };
	unsigned long bounced[DIV_ROUND_UP(LZ4_MAX_DISTANCE_PAGES,
					   BITS_PER_LONG)] = { 0 };
	unsigned int lz4_max_distance_pages =
		EROFS_SB(rq->sb)->lz4.max_distance_pages ? :
		LZ4_MAX_DISTANCE_PAGES;
	void *kaddr = NULL;
	unsigned int i, j, top;

	top = 0;
	for (i = j = 0; i < nr; ++i, ++j) {
		struct page *const page = rq->out[i];
		struct page *victim;

		if (j >= lz4_max_distance_pages)
			j = 0;

		if (test_bit(j, bounced)) {
			DBG_BUGON(i < lz4_max_distance_pages);
			DBG_BUGON(top >= lz4_max_distance_pages);
			availables[top++] = rq->out[i - lz4_max_distance_pages];
		}

		if (page) {
			__clear_bit(j, bounced);
			if (!PageHighMem(page)) {
				if (!i) {
					kaddr = page_address(page);
					continue;
				}
				if (kaddr &&
				    kaddr + PAGE_SIZE == page_address(page)) {
					kaddr += PAGE_SIZE;
					continue;
				}
			}
			kaddr = NULL;
			continue;
		}
		kaddr = NULL;
		__set_bit(j, bounced);

		if (top) {
			victim = availables[--top];
			get_page(victim);
		} else {
			victim = erofs_allocpage(pagepool, GFP_KERNEL, false);
			if (!victim)
				return -ENOMEM;
			victim->mapping = Z_EROFS_MAPPING_STAGING;
		}
		rq->out[i] = victim;
	}
	return kaddr ? 1 : 0;
}

static void *z_erofs_lz4_handle_inplace_io(struct z_erofs_decompress_req *rq,
					   void *inpage,
					   unsigned int *inputmargin,
					   int *maptype,
					   bool support_0padding)
{
	unsigned int nrpages_in, nrpages_out;
	unsigned int ofull, oend, inputsize, total, i, j;
	struct page **in;
	void *src, *tmp;

	inputsize = rq->inputsize;
	nrpages_in = PAGE_ALIGN(inputsize) >> PAGE_SHIFT;
	oend = rq->pageofs_out + rq->outputsize;
	ofull = PAGE_ALIGN(oend);
	nrpages_out = ofull >> PAGE_SHIFT;

	if (rq->inplace_io) {
		if (rq->partial_decoding || !support_0padding ||
		    nrpages_out < nrpages_in ||
		    ofull - oend < LZ4_DECOMPRESS_INPLACE_MARGIN(inputsize))
			goto docopy;

		for (i = 0; i < nrpages_in; ++i) {
			DBG_BUGON(rq->in[i] == NULL);
			for (j = 0; j < nrpages_out - nrpages_in + i; ++j)
				if (rq->out[j] == rq->in[i])
					goto docopy;
		}
	}

	if (nrpages_in <= 1) {
		*maptype = 0;
		return inpage;
	}
	kunmap_atomic(inpage);
	might_sleep();
	src = erofs_vm_map_ram(rq->in, nrpages_in);
	if (!src)
		return ERR_PTR(-ENOMEM);
	*maptype = 1;
	return src;

docopy:
	in = rq->in;
	src = erofs_get_pcpubuf(nrpages_in);
	if (!src) {
		DBG_BUGON(1);
		kunmap_atomic(inpage);
		return ERR_PTR(-EFAULT);
	}

	tmp = src;
	total = rq->inputsize;
	while (total) {
		unsigned int page_copycnt =
			min_t(unsigned int, total, PAGE_SIZE - *inputmargin);

		if (!inpage)
			inpage = kmap_atomic(*in);
		memcpy(tmp, inpage + *inputmargin, page_copycnt);
		kunmap_atomic(inpage);
		inpage = NULL;
		tmp += page_copycnt;
		total -= page_copycnt;
		++in;
		*inputmargin = 0;
	}
	*maptype = 2;
	return src;
}

static int z_erofs_lz4_decompress_mem(struct z_erofs_decompress_req *rq,
				      u8 *out)
{
	unsigned int inputmargin, orig_inputsize;
	u8 *headpage, *src;
	bool support_0padding;
	int ret, maptype;

	DBG_BUGON(*rq->in == NULL);
	headpage = kmap_atomic(*rq->in);
	inputmargin = 0;
	support_0padding = false;
	orig_inputsize = rq->inputsize;

	if (erofs_sb_has_lz4_0padding(EROFS_SB(rq->sb))) {
		support_0padding = true;

		while (!headpage[inputmargin & ~PAGE_MASK])
			if (!(++inputmargin & ~PAGE_MASK))
				break;

		if (inputmargin >= rq->inputsize) {
			kunmap_atomic(headpage);
			return -EIO;
		}
	}

	rq->inputsize -= inputmargin;
	src = z_erofs_lz4_handle_inplace_io(rq, headpage, &inputmargin,
					    &maptype, support_0padding);
	if (IS_ERR(src))
		return PTR_ERR(src);

	if (rq->partial_decoding || !support_0padding)
		ret = LZ4_decompress_safe_partial(src + inputmargin, out,
				rq->inputsize, rq->outputsize, rq->outputsize);
	else
		ret = LZ4_decompress_safe(src + inputmargin, out,
					  rq->inputsize, rq->outputsize);

	if (ret != rq->outputsize) {
		erofs_err(rq->sb, "failed to decompress %d in[%u, %u] out[%u]",
			  ret, rq->inputsize, inputmargin, rq->outputsize);
		print_hex_dump(KERN_DEBUG, "[ in]: ", DUMP_PREFIX_OFFSET,
			       16, 1, src + inputmargin, rq->inputsize, true);
		print_hex_dump(KERN_DEBUG, "[out]: ", DUMP_PREFIX_OFFSET,
			       16, 1, out, rq->outputsize, true);
		if (ret >= 0)
			memset(out + ret, 0, rq->outputsize - ret);
		ret = -EIO;
	} else {
		ret = 0;
	}

	if (maptype == 0) {
		kunmap_atomic(src);
	} else if (maptype == 1) {
		vm_unmap_ram(src, PAGE_ALIGN(orig_inputsize) >> PAGE_SHIFT);
	} else if (maptype == 2) {
		erofs_put_pcpubuf(src);
	} else {
		DBG_BUGON(1);
		return -EFAULT;
	}
	return ret;
}

static int z_erofs_lz4_decompress(struct z_erofs_decompress_req *rq,
				  struct list_head *pagepool)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	unsigned int dst_maptype;
	void *dst;
	int ret;

	if (rq->inputsize <= PAGE_SIZE && nrpages_out == 1 && !rq->inplace_io) {
		DBG_BUGON(!*rq->out);
		dst = kmap_atomic(*rq->out);
		dst_maptype = 0;
		goto dstmap_out;
	}

	ret = z_erofs_lz4_prepare_dstpages(rq, pagepool);
	if (ret < 0)
		return ret;
	if (ret) {
		dst = page_address(*rq->out);
		dst_maptype = 1;
		goto dstmap_out;
	}

	dst = erofs_vm_map_ram(rq->out, nrpages_out);
	if (!dst)
		return -ENOMEM;
	dst_maptype = 2;

dstmap_out:
	ret = z_erofs_lz4_decompress_mem(rq, dst + rq->pageofs_out);

	if (!dst_maptype)
		kunmap_atomic(dst);
	else if (dst_maptype == 2)
		vm_unmap_ram(dst, nrpages_out);
	return ret;
}

static int z_erofs_shifted_transform(struct z_erofs_decompress_req *rq,
				     struct list_head *pagepool)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int righthalf = PAGE_SIZE - rq->pageofs_out;
	unsigned char *src, *dst;

	(void)pagepool;
	if (nrpages_out > 2) {
		DBG_BUGON(1);
		return -EIO;
	}

	if (rq->out[0] == *rq->in) {
		DBG_BUGON(nrpages_out != 1);
		return 0;
	}

	src = kmap_atomic(*rq->in);
	if (rq->out[0]) {
		dst = kmap_atomic(rq->out[0]);
		memcpy(dst + rq->pageofs_out, src, righthalf);
		kunmap_atomic(dst);
	}

	if (nrpages_out == 2) {
		DBG_BUGON(!rq->out[1]);
		if (rq->out[1] == *rq->in) {
			memmove(src, src + righthalf, rq->pageofs_out);
		} else {
			dst = kmap_atomic(rq->out[1]);
			memcpy(dst, src + righthalf, rq->pageofs_out);
			kunmap_atomic(dst);
		}
	}
	kunmap_atomic(src);
	return 0;
}

struct z_erofs_decompressor {
	int (*decompress)(struct z_erofs_decompress_req *rq,
			  struct list_head *pagepool);
	char *name;
};

static struct z_erofs_decompressor decompressors[] = {
	[Z_EROFS_COMPRESSION_SHIFTED] = {
		.decompress = z_erofs_shifted_transform,
		.name = "shifted"
	},
	[Z_EROFS_COMPRESSION_LZ4] = {
		.decompress = z_erofs_lz4_decompress,
		.name = "lz4"
	},
};

int z_erofs_decompress(struct z_erofs_decompress_req *rq,
		       struct list_head *pagepool)
{
	return decompressors[rq->alg].decompress(rq, pagepool);
}
