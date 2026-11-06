#include <linux/module.h>

#include "common/huf.h"
#include "common/fse.h"
#include "common/zstd_internal.h"

#undef ZSTD_isError
EXPORT_SYMBOL_GPL(FSE_readNCount);
EXPORT_SYMBOL_GPL(HUF_readStats);
EXPORT_SYMBOL_GPL(HUF_readStats_wksp);
EXPORT_SYMBOL_GPL(ZSTD_isError);
EXPORT_SYMBOL_GPL(ZSTD_getErrorName);
EXPORT_SYMBOL_GPL(ZSTD_getErrorCode);
EXPORT_SYMBOL_GPL(ZSTD_customMalloc);
EXPORT_SYMBOL_GPL(ZSTD_customCalloc);
EXPORT_SYMBOL_GPL(ZSTD_customFree);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Zstd Common");
