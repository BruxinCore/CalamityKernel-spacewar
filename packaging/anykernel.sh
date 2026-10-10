# AnyKernel3 package for Nothing Phone (1)
properties() { '
kernel.string=Caly-Rice-Beta by BruxinCore
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=1
device.name1=Spacewar
device.name2=spacewar
device.name3=NP1
supported.versions=
supported.patchlevels=
'; }

BLOCK=boot;
IS_SLOT_DEVICE=auto;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

. tools/ak3-core.sh;
dump_boot;
write_boot;
