properties() { '
kernel.string=CALY-Base 6.12.81 LTS by NerokiStyle
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=1
device.name1=Spacewar
device.name2=spacewar
device.name3=NP1
device.name4=A063
device.name5=lahaina
supported.versions=
supported.patchlevels=
'; }

BLOCK=boot;
IS_SLOT_DEVICE=auto;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

. tools/ak3-core.sh;
ui_print " - Official source: https://github.com/BruxinCore/CalamityKernel-spacewar";

dump_boot;
write_boot;
rm -f "$AKHOME/Image";

if [ -f "$AKHOME/caly_payload/dtbo.img" ]; then
  DTBO_BLK="";
  for p in "/dev/block/by-name/dtbo$SLOT" "/dev/block/bootdevice/by-name/dtbo$SLOT" "/dev/block/by-name/dtbo" "/dev/block/bootdevice/by-name/dtbo"; do
    if [ -e "$p" ]; then
      DTBO_BLK="$p";
      break;
    fi;
  done;
  if [ -n "$DTBO_BLK" ]; then
    ui_print " - Flashing CALY 6.12 DTBO to $DTBO_BLK...";
    dd if="$AKHOME/caly_payload/dtbo.img" of="$DTBO_BLK" bs=4096 || abort "Failed to flash dtbo";
  else
    abort "DTBO partition not found";
  fi;
fi;

BLOCK=vendor_boot;
IS_SLOT_DEVICE=auto;
RAMDISK_COMPRESSION=auto;
NO_MAGISK_CHECK=1;
PATCH_VBMETA_FLAG=auto;
reset_ak;

dump_boot;

update_vrd() {
  local base="$1";
  rm -rf "$base/lib/modules";
  mkdir -p "$base/lib/modules";
  cp -af "$AKHOME/caly_payload/modules/"* "$base/lib/modules/";
  ln -sf . "$base/lib/modules/5.4-gki";
  ln -sf . "$base/lib/modules/6.12.81-CALY-Daitaku";
  ln -sf . "$base/lib/modules/6.12.81-CALY-Daitaku-Stable";
  ln -sf . "$base/lib/modules/6.12.81-CALY-Base";
  if [ -f "$AKHOME/caly_payload/firmware/tfa98xx.cnt" ]; then
    mkdir -p "$base/lib/firmware";
    cp -af "$AKHOME/caly_payload/firmware/tfa98xx.cnt" "$base/lib/firmware/tfa98xx.cnt";
  fi;
}

if [ -f "$SPLITIMG/ramdisk.cpio" ] && [ -d "$RAMDISK" ]; then
  ui_print " - Updating vendor_boot ramdisk modules (preserving ROM fstab)...";
  update_vrd "$RAMDISK";
elif [ -d "$VENDORRD" ]; then
  for d in "$VENDORRD"/*; do
    if [ -d "$d/lib/modules" ]; then
      ui_print " - Updating vendor_ramdisk modules in $(basename "$d")...";
      update_vrd "$d";
    fi;
  done;
fi;

if [ -f "$AKHOME/caly_payload/dtb" ]; then
  cp -f "$AKHOME/caly_payload/dtb" "$SPLITIMG/dtb";
fi;

if [ -f "$SPLITIMG/header" ]; then
  for tok in clk_ignore_unused pd_ignore_unused fw_devlink=permissive; do
    grep -q "$tok" "$SPLITIMG/header" || sed -i "s|^cmdline=.*|& $tok|" "$SPLITIMG/header";
  done;
fi;

if [ -f "$BOOTIMG" ]; then
  printf '\000\000\200\003\000\000\000\000' | dd of="$BOOTIMG" bs=1 seek=2104 conv=notrunc 2>/dev/null;
fi;

write_boot;
