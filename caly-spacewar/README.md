# CALY-Base 6.12.81 LTS for Nothing Phone (1) (Spacewar / SM7325-AE)

Clean, tuning-free Linux 6.12.81 LTS (Android 16 GKI base) bring-up branch for Nothing Phone (1) (`Spacewar`, Snapdragon 778G+ `SM7325-AE` / `Yupik`).

## Purpose
- Serves as the clean, stable foundation for new 6.12 CalamityKernel flavours without carrying flavour-specific I/O, scheduler, network, or F2FS tunings.
- Includes 100% of the verified Spacewar hardware bring-up, GKI KABI compatibility (`17862/17862` symbols), universal ROM support (Nothing OS, AOSP, LineageOS, HyperOS), zero VINTF boot warning, full SM7325 Cluster Deep Sleep (`D0/D1/D2` via `qcom-cpu-lpm` + `qcom_stats`), WCN6750 Runtime PM `PS_HOLD` reset fix (`caly_rproc_boot`), 256KB `ramoops` ECC, and Android 14+ MM stability fix (`THP` default `never`, `compaction_proactiveness=0`).
- Clean upstream Linux defaults (`swappiness=60`, `watermark_scale_factor=10`, default GKI I/O & TCP stack).
- Optional root via official `backslashxx/KernelSU`.

## Repository Layout
- Upstream base: full Linux + Google Android Common Kernel `android16-6.12` history up to `8a69376da` (`6.12.81`)
- Core commits: Android 14 `pidfs` & VINTF compatibility, Spacewar UAPI keycodes, clean `UTS_RELEASE`, KernelSU hooks, Android THP/compaction fix, and `spacewar_caly_base_defconfig`
- `caly-spacewar/dts/`: Spacewar 6.12 DTB (`yupik-spacewar-6.12.dts` with cluster LPM `D0/D1/D2`) and DTBO (`spacewar-dtbo.dts` with ramoops ECC)
- `caly-spacewar/modules/`: Authorial 6.12 compatibility drivers (`caly_ashmem`, `caly_rproc_boot`, `caly_compat`)
- `caly-spacewar/patches/`: All 13 surgical diffs against upstream CLO `LA.VENDOR.16.2.8.r1` repositories + `CLO_SOURCES.md`
- `caly-spacewar/packaging/anykernel3/`: Universal multi-partition `AnyKernel3` installer (`boot`, `dtbo`, `vendor_boot`) and `modules.load`

## Credits & Upstream Authorship
- **Linux Kernel & Android16-6.12 GKI:** Linux kernel contributors and Google Android Common Kernel (`android.googlesource.com/kernel/common`, base `8a69376da`)
- **Qualcomm SoC, LPM & Techpack Drivers:** Qualcomm Technologies, Inc. / CodeLinaro (`LA.VENDOR.16.2.8.r1`)
- **Nothing Phone (1) Hardware Drivers:** NothingOSS, FocalTech, Goodix, NXP (TFA98xx), Awinic, AAC RichTap
- **KernelSU:** `backslashxx/KernelSU` and `tiann/KernelSU`
- **AnyKernel3:** `osm0sis/AnyKernel3`
- **Spacewar 6.12 Bring-Up, Forensics, Modules & Patches:** NerokiStyle (`BruxinCore`)

## License, Attribution & Official Releases
- Licensed under GNU GPLv2. Any fork, port, or derivative using Spacewar 6.12 device trees, authorial modules (`caly_rproc_boot`, `caly_ashmem`, `caly_compat`), or surgical patches must preserve original authorship and commit history (`NerokiStyle / BruxinCore` and all upstream contributors).
- Official releases and SHA-256 checksums are published exclusively at: `https://github.com/BruxinCore/CalamityKernel-spacewar/releases`
