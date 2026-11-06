#ifndef ZSTD_DDICT_H
#define ZSTD_DDICT_H

#include "../common/zstd_deps.h"
#include <linux/zstd.h>

const void* ZSTD_DDict_dictContent(const ZSTD_DDict* ddict);
size_t ZSTD_DDict_dictSize(const ZSTD_DDict* ddict);

void ZSTD_copyDDictParameters(ZSTD_DCtx* dctx, const ZSTD_DDict* ddict);

#endif
