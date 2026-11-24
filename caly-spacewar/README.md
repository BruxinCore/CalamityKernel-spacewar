# CALY-Daitaku 6.12.81 LTS for Nothing Phone (1) (Spacewar / SM7325-AE)

100% authorial bring-up of Linux 6.12.81 LTS (Android 16 GKI base) for Nothing Phone (1) (`Spacewar`, Snapdragon 778G+ `SM7325-AE` / `Yupik`).

## Highlights
- Clean kernel release: `6.12.81-CALY-Daitaku-Stable` with 100% GKI KABI compatibility (`17862/17862` symbols intact)
- Universal ROM support across Nothing OS, AOSP, LineageOS, and HyperOS (preserves installed ROM `fstab.*`)
- Zero VINTF boot warning dialog without touching `/system`
- Full hardware bring-up: 120Hz Visionox AMOLED, Adreno 642L GPU, FocalTech FT3680 touch, Goodix optical UDFPS, Wi-Fi 6/6E, 4G/5G RIL + Mobile Data, Bluetooth, PM7325B battery + USB-C role-switch, 36/36 sensors, WCD938x + TFA98xx stereo audio + camcorder-mic, Spectra ISP camera, Iris2 HW video encode/decode, 36 thermal zones + 25 cooling devices (TSENS + LMH + BCL)
- Full SM7325 Deep Sleep (`D0/D1/D2` cluster collapse via `qcom-cpu-lpm` + `qcom_stats`) and permanent WCN6750 Runtime PM fix (`caly_rproc_boot`) eliminating silent `PS_HOLD` hard resets
- Persistent `ramoops` crash logging (256KB record buffer with 16-byte Reed-Solomon ECC)
- CALY-Daitaku tuning: Samsung SSG I/O scheduler, TCP BBR + FQ default, ZRAM LZ4, SELinux AVC ratelimit, Binder -10 sync priority floor, F2FS fsync scaling, Android-tuned VM defaults (`swappiness=60`, `watermark_scale_factor=10`, THP default `never`, `compaction_proactiveness=0`)
- Optional root via official `backslashxx/KernelSU`

## Repository Layout
- Upstream base: full Linux + Google Android Common Kernel `android16-6.12` history up to `8a69376da` (`6.12.81`)
- Core kernel commits: SSG I/O scheduler, GKI-safe elevator mapping, SELinux/Binder/F2FS tuning, Android 14 `pidfs` & VINTF compatibility, Spacewar UAPI keycodes, KernelSU hooks, and `spacewar_caly_daitaku_defconfig`
- `caly-spacewar/dts/`: Spacewar 6.12 DTB (`yupik-spacewar-6.12.dts` with cluster LPM `D0/D1/D2`) and DTBO (`spacewar-dtbo.dts` with ramoops ECC) sources
- `caly-spacewar/modules/`: Authorial 6.12 compatibility drivers (`caly_ashmem`, `caly_rproc_boot`, `caly_compat`)
- `caly-spacewar/patches/`: Per-subsystem surgical diffs against upstream CLO `LA.VENDOR.16.2.8.r1` repositories + `CLO_SOURCES.md`
- `caly-spacewar/packaging/anykernel3/`: Universal multi-partition `AnyKernel3` installer (`boot`, `dtbo`, `vendor_boot`) and `modules.load` (including `qcom_lpm.ko` and `qcom_stats.ko`)

## Credits & Upstream Authorship
- **Linux Kernel & Android16-6.12 GKI:** Linux kernel contributors and Google Android Common Kernel (`android.googlesource.com/kernel/common`, base `8a69376da`)
- **Qualcomm SoC, LPM & Techpack Drivers:** Qualcomm Technologies, Inc. / CodeLinaro (`LA.VENDOR.16.2.8.r1`, including `qcom_lpm`, `qcom_stats`, `qcom_rpmh`, `icnss2`, `qcacld-3.0`, `msm_drm`, `msm_kgsl`, `camera`, `video`, `audio`, `dataipa`)
- **Samsung Generic I/O Scheduler (SSG):** Jisoo Oh, Manjong Lee, Changheun Lee (Samsung Electronics)
- **Nothing Phone (1) Hardware Drivers:** NothingOSS, FocalTech, Goodix, NXP (TFA98xx), Awinic, AAC RichTap
- **KernelSU:** `backslashxx/KernelSU` and `tiann/KernelSU`
- **AnyKernel3:** `osm0sis/AnyKernel3`
- **Spacewar 6.12 Bring-Up, Forensics, Modules, Patches & Tuning:** NerokiStyle (`BruxinCore`)

## License, Attribution & Official Releases
- Licensed under GNU GPLv2. Any fork, port, or derivative using Spacewar 6.12 device trees, authorial modules (`caly_rproc_boot`, `caly_ashmem`, `caly_compat`), or surgical patches must preserve original authorship and commit history (`NerokiStyle / BruxinCore` and all upstream contributors).
- Official signed/verified releases and SHA-256 checksums are published exclusively at: `https://github.com/BruxinCore/CalamityKernel-spacewar/releases`
