#!/usr/bin/env bash
set -euo pipefail
installer=${1:?Pass the AnyKernel installer path}
bash -n "$installer"
# Pinned AnyKernel3 reads uppercase settings; lowercase names produce an empty
# partition target. Keep slot detection dynamic rather than hardcoding boot_a.
for setting in 'BLOCK=boot;' 'IS_SLOT_DEVICE=auto;' 'RAMDISK_COMPRESSION=auto;' 'PATCH_VBMETA_FLAG=auto;'; do
  grep -Fxq "$setting" "$installer" || { echo "ERROR: missing AnyKernel setting: $setting" >&2; exit 1; }
done
if grep -Eq '^[[:space:]]*(block|is_slot_device|ramdisk_compression|patch_vbmeta_flag)=' "$installer"; then
  echo 'ERROR: lowercase AnyKernel settings are incompatible with the pinned core' >&2
  exit 1
fi
