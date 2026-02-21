# 🌌 Unofficial NOS 3.0 Spacewar Kernel

![Build Status](https://img.shields.io/github/actions/workflow/status/zerofrip/Spacewar_NOS3.0_Kernel/update_susfs.yml?branch=sm7325/v/mr&label=Build&style=for-the-badge)
![KernelSU-Next](https://img.shields.io/badge/KernelSU--Next-Integrated-blueviolet?style=for-the-badge)
![SUSFS](https://img.shields.io/badge/SUSFS-v2.0.0-orange?style=for-the-badge)

An unofficial NOS 3.0 kernel for Nothing Phone (1) (**spacewar**), featuring the latest root hiding and system bypass capabilities.

---

## 🚀 Key Features

- **KernelSU-Next**: Integrated with the latest `dev_susfs` branch from [rifsxd](https://github.com/rifsxd/KernelSU-Next).
- **SUSFS v2.0.0**: Optimized for the 5.4 kernel. Supports advanced directory, mount, and kstat hiding.
- **WireGuard**: Built-in `wireguard-linux-compat` for high-performance, secure networking.
- **CI/CD Automation**: Automatically checks for NothingOSS upstream updates monthly, re-patches, builds, and releases.

---

## 🛠 Installation

### 1. Using boot.img
Download the latest `boot.img` from the [Releases](https://github.com/zerofrip/Spacewar_NOS3.0_Kernel/releases) page.
```bash
# Flash via Fastboot in Bootloader mode
fastboot flash boot boot.img
```

### 2. Using Kernel Managers
Download `Uo_Spacewar_NOS3.0_Kernel_*.zip` from [Releases](https://github.com/zerofrip/Spacewar_NOS3.0_Kernel/releases) and flash it using your preferred Kernel Manager (e.g., Franco Kernel Manager, SmartPack).

---

## 🔄 Automatic Updates

You can register the following URL in [Franco Kernel Manager](https://play.google.com/store/apps/details?id=com.franco.kernel) (or similar apps) to receive automatic update notifications.

```text
https://raw.githubusercontent.com/zerofrip/Spacewar_NOS3.0_Kernel/refs/heads/sm7325/v/mr/kernel-downloads.json
```

---

## 🤝 Credits & Acknowledgements

- **Upstream Kernel**: [NothingOSS](https://github.com/NothingOSS/android_kernel_msm-5.4_nothing_sm7325/tree/sm7325/v/mr)
- **Reference Kernel**: [Christian <kimo> B.](https://github.com/kimocoder/android_kernel_nothing_sm7325)
- **KernelSU-Next**: [rifsxd](https://github.com/rifsxd/KernelSU-Next)
- **SUSFS**: [simonpunk](https://gitlab.com/simonpunk/susfs4ksu)
- **Boot Image Tool**: [Android_boot_image_editor](https://github.com/cfig/Android_boot_image_editor)
- **Stock Images source**: [spike0en/nothing_archive](https://github.com/spike0en/nothing_archive)
- **Special Thanks**: `⫷t⫸⫷u⫸⫷y⫸⫷i⫸⫷e⫸` (Tester)

---

> [!CAUTION]
> Use this kernel at your own risk. I am not responsible for bricked devices or data loss. An unlocked bootloader is required.
