#ifndef ZSTD_LDM_H
#define ZSTD_LDM_H

#include "zstd_compress_internal.h"
#include <linux/zstd.h>

#define ZSTD_LDM_DEFAULT_WINDOW_LOG ZSTD_WINDOWLOG_LIMIT_DEFAULT

void ZSTD_ldm_fillHashTable(
            ldmState_t* state, const BYTE* ip,
            const BYTE* iend, ldmParams_t const* params);

size_t ZSTD_ldm_generateSequences(
            ldmState_t* ldms, rawSeqStore_t* sequences,
            ldmParams_t const* params, void const* src, size_t srcSize);

size_t ZSTD_ldm_blockCompress(rawSeqStore_t* rawSeqStore,
            ZSTD_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD_REP_NUM],
            ZSTD_paramSwitch_e useRowMatchFinder,
            void const* src, size_t srcSize);

void ZSTD_ldm_skipSequences(rawSeqStore_t* rawSeqStore, size_t srcSize,
    U32 const minMatch);

void ZSTD_ldm_skipRawSeqStoreBytes(rawSeqStore_t* rawSeqStore, size_t nbBytes);

size_t ZSTD_ldm_getTableSize(ldmParams_t params);

size_t ZSTD_ldm_getMaxNbSeq(ldmParams_t params, size_t maxChunkSize);

void ZSTD_ldm_adjustParameters(ldmParams_t* params,
                               ZSTD_compressionParameters const* cParams);

#endif
