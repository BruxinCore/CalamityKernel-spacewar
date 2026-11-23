# CALY-Daitaku 6.12.81 LTS for Nothing Phone (1) (Spacewar / SM7325-AE)

100% authorial bring-up of Linux 6.12.81 LTS (Android 16 GKI base) for Nothing Phone (1) (`Spacewar`, Snapdragon 778G+ `SM7325-AE` / `Yupik`).

## Highlights
- Clean kernel release: `6.12.81-CALY-Daitaku` with 100% GKI KABI compatibility (`17862/17862` symbols intact)
- Universal ROM support across Nothing OS, AOSP, LineageOS, and HyperOS (preserves installed ROM `fstab.*`)
- Zero VINTF boot warning dialog without touching `/system`
- Full hardware bring-up: 120Hz Visionox AMOLED, Adreno 642L GPU, FocalTech FT3680 touch, Goodix optical UDFPS, Wi-Fi 6/6E, 4G/5G RIL + Mobile Data, Bluetooth, PM7325B battery + USB-C role-switch, 36/36 sensors, WCD938x + TFA98xx stereo audio + camcorder-mic, Spectra ISP camera, Iris2 HW video encode/decode, 36 thermal zones + 25 cooling devices (TSENS + LMH + BCL)
- Stable SM7325 CPU idle: direct PSCI per-CPU power collapse (`0x40000003`) across all 8 cores with `power-domain-cluster` isolated for zero idle hard resets
- CALY-Daitaku tuning: Samsung SSG I/O scheduler, TCP BBR + FQ default, ZRAM LZ4, SELinux AVC ratelimit, Binder -10 sync priority floor, F2FS fsync scaling, VM tuning
- Optional root via official `backslashxx/KernelSU`

## Repository Layout
- Upstream base: full Linux + Google Android Common Kernel `android16-6.12` history up to `8a69376da` (`6.12.81`)
- Core kernel commits: SSG I/O scheduler, GKI-safe elevator mapping, SELinux/Binder/F2FS/VM tuning, Android 14 `pidfs` & VINTF compatibility, Spacewar UAPI keycodes, KernelSU hooks, and `spacewar_caly_daitaku_defconfig`
- `caly-spacewar/dts/`: Spacewar 6.12 DTB (`yupik-spacewar-6.12.dts`) and DTBO (`spacewar-dtbo.dts`) sources
- `caly-spacewar/modules/`: Authorial 6.12 compatibility drivers (`caly_ashmem`, `caly_rproc_boot`, `caly_compat`)
- `caly-spacewar/patches/`: Per-subsystem surgical diffs against upstream CLO `LA.VENDOR.16.2.8.r1` repositories + `CLO_SOURCES.md`
- `caly-spacewar/packaging/anykernel3/`: Universal multi-partition `AnyKernel3` installer (`boot`, `dtbo`, `vendor_boot`) and `modules.load`

## Credits & Upstream Authorship
- **Linux Kernel & Android16-6.12 GKI:** Linux kernel contributors and Google Android Common Kernel (`android.googlesource.com/kernel/common`, base `8a69376da`)
- **Qualcomm SoC & Techpack Drivers:** Qualcomm Technologies, Inc. / CodeLinaro (`LA.VENDOR.16.2.8.r1`)
- **Samsung Generic I/O Scheduler (SSG):** Jisoo Oh, Manjong Lee, Changheun Lee (Samsung Electronics)
- **Nothing Phone (1) Hardware Drivers:** NothingOSS, FocalTech, Goodix, NXP (TFA98xx), Awinic, AAC RichTap
- **KernelSU:** `backslashxx/KernelSU` and `tiann/KernelSU`
- **AnyKernel3:** `osm0sis/AnyKernel3`
- **Spacewar 6.12 Bring-Up, Modules, Patches & Tuning:** NerokiStyle (`BruxinCore`)
