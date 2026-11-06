#ifndef ZSTD_DEC_BLOCK_H
#define ZSTD_DEC_BLOCK_H

#include "../common/zstd_deps.h"
#include <linux/zstd.h>
#include "../common/zstd_internal.h"
#include "zstd_decompress_internal.h"

typedef enum {
    not_streaming = 0,
    is_streaming = 1
} streaming_operation;

size_t ZSTD_decompressBlock_internal(ZSTD_DCtx* dctx,
                               void* dst, size_t dstCapacity,
                         const void* src, size_t srcSize, const int frame, const streaming_operation streaming);

void ZSTD_buildFSETable(ZSTD_seqSymbol* dt,
             const short* normalizedCounter, unsigned maxSymbolValue,
             const U32* baseValue, const U8* nbAdditionalBits,
                   unsigned tableLog, void* wksp, size_t wkspSize,
                   int bmi2);

#endif
