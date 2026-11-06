#ifndef LINUX_ZSTD_H
#define LINUX_ZSTD_H

#include <linux/types.h>
#include <linux/zstd_errors.h>
#include <linux/zstd_lib.h>

size_t zstd_compress_bound(size_t src_size);

unsigned int zstd_is_error(size_t code);

typedef ZSTD_ErrorCode zstd_error_code;

zstd_error_code zstd_get_error_code(size_t code);

const char *zstd_get_error_name(size_t code);

int zstd_min_clevel(void);

int zstd_max_clevel(void);

int zstd_default_clevel(void);

typedef ZSTD_customMem zstd_custom_mem;

typedef ZSTD_dictLoadMethod_e zstd_dict_load_method;

typedef ZSTD_dictContentType_e zstd_dict_content_type;

typedef ZSTD_strategy zstd_strategy;

typedef ZSTD_compressionParameters zstd_compression_parameters;

typedef ZSTD_frameParameters zstd_frame_parameters;

typedef ZSTD_parameters zstd_parameters;

zstd_parameters zstd_get_params(int level,
	unsigned long long estimated_src_size);

zstd_compression_parameters zstd_get_cparams(int level,
	unsigned long long estimated_src_size, size_t dict_size);

typedef ZSTD_CCtx zstd_cctx;

size_t zstd_cctx_workspace_bound(const zstd_compression_parameters *parameters);

zstd_cctx *zstd_init_cctx(void *workspace, size_t workspace_size);

size_t zstd_compress_cctx(zstd_cctx *cctx, void *dst, size_t dst_capacity,
	const void *src, size_t src_size, const zstd_parameters *parameters);

zstd_cctx *zstd_create_cctx_advanced(zstd_custom_mem custom_mem);

size_t zstd_free_cctx(zstd_cctx* cctx);

typedef ZSTD_CDict zstd_cdict;

zstd_cdict *zstd_create_cdict_byreference(const void *dict, size_t dict_size,
					  zstd_compression_parameters cparams,
					  zstd_custom_mem custom_mem);

size_t zstd_free_cdict(zstd_cdict* cdict);

size_t zstd_compress_using_cdict(zstd_cctx *cctx, void *dst,
	size_t dst_capacity, const void *src, size_t src_size,
	const zstd_cdict *cdict);

typedef ZSTD_DCtx zstd_dctx;

size_t zstd_dctx_workspace_bound(void);

zstd_dctx *zstd_init_dctx(void *workspace, size_t workspace_size);

size_t zstd_decompress_dctx(zstd_dctx *dctx, void *dst, size_t dst_capacity,
	const void *src, size_t src_size);

typedef ZSTD_DDict zstd_ddict;

zstd_ddict *zstd_create_ddict_byreference(const void *dict, size_t dict_size,
					  zstd_custom_mem custom_mem);

size_t zstd_free_ddict(zstd_ddict *ddict);

zstd_dctx *zstd_create_dctx_advanced(zstd_custom_mem custom_mem);

size_t zstd_free_dctx(zstd_dctx *dctx);

size_t zstd_decompress_using_ddict(zstd_dctx *dctx,
	void *dst, size_t dst_capacity, const void *src, size_t src_size,
	const zstd_ddict *ddict);

typedef ZSTD_inBuffer zstd_in_buffer;

typedef ZSTD_outBuffer zstd_out_buffer;

typedef ZSTD_CStream zstd_cstream;

size_t zstd_cstream_workspace_bound(const zstd_compression_parameters *cparams);

zstd_cstream *zstd_init_cstream(const zstd_parameters *parameters,
	unsigned long long pledged_src_size, void *workspace, size_t workspace_size);

size_t zstd_reset_cstream(zstd_cstream *cstream,
	unsigned long long pledged_src_size);

size_t zstd_compress_stream(zstd_cstream *cstream, zstd_out_buffer *output,
	zstd_in_buffer *input);

size_t zstd_flush_stream(zstd_cstream *cstream, zstd_out_buffer *output);

size_t zstd_end_stream(zstd_cstream *cstream, zstd_out_buffer *output);

typedef ZSTD_DStream zstd_dstream;

size_t zstd_dstream_workspace_bound(size_t max_window_size);

zstd_dstream *zstd_init_dstream(size_t max_window_size, void *workspace,
	size_t workspace_size);

size_t zstd_reset_dstream(zstd_dstream *dstream);

size_t zstd_decompress_stream(zstd_dstream *dstream, zstd_out_buffer *output,
	zstd_in_buffer *input);

size_t zstd_find_frame_compressed_size(const void *src, size_t src_size);

typedef ZSTD_frameHeader zstd_frame_header;

size_t zstd_get_frame_header(zstd_frame_header *params, const void *src,
	size_t src_size);

#endif
