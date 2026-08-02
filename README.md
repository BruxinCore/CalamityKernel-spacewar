<div align="center">
  <h1>🌌 CalamityKernel (Spacewar)</h1>
  <p><b>An aggressively optimized, ultra-stealth kernel for the Nothing Phone (1)</b></p>
  
  [![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen?style=for-the-badge)](https://github.com/BruxinCore/CalamityKernel-spacewar)
  [![KernelSU](https://img.shields.io/badge/Root-KernelSU_Syscall_Tampered-red?style=for-the-badge)](https://github.com/backslashxx/KernelSU)
  [![Android](https://img.shields.io/badge/Android-13%2F14%2F15-blue?style=for-the-badge)](#)
  [![License](https://img.shields.io/badge/License-GPL_v2-black?style=for-the-badge)](#)
</div>

<br>

**CalamityKernel** (Codename: *Caly-Tachyon*) is a high-performance, strictly debloated custom kernel built specifically for the Nothing Phone 1 (Spacewar). Engineered from the ground up for zero-latency UI fluidity, robust battery management, and military-grade root concealment.

---

## 🚀 Architectural Highlights

### ⚡ Performance & CPU
* **1000Hz Timer Frequency (`HZ_1000`)**: Reduced scheduler latency to an absolute minimum for ultra-responsive touch feedback.
* **WALT CPU Input Boost**: Custom WALT integration for extreme UI fluidity. Aggressively scales CPU frequencies upon touch inputs to prevent micro-stutters during heavy gaming or rapid scrolling.
* **PELT 8ms Half-life**: The Per-Entity Load Tracking is hardcoded to an 8ms half-life (Android default is 32ms), ensuring instantaneous CPU scaling and scheduling.

### 💾 Memory & I/O
* **SSG I/O Scheduler**: Replaced stock schedulers with the high-throughput SSG scheduler, providing superior concurrent read/write speeds for mobile flash storage.
* **ZRAM with Native ZSTD**: Swapped the default LZO compressor for ZSTD natively in the `zram_drv` C code. Massively improves memory compression ratios and decompression speeds.
* **Full F2FS Compression Suite**: Native support for LZO, LZ4, ZSTD, and LZORLE hardware compression algorithms for extreme read speeds and storage preservation.

### 🔋 Battery & Networking
* **TCP Westwood+ (Default Congestion Control)**: Engineered specifically for wireless networks (Wi-Fi/5G). It completely ignores signal interference to lock your ping and eliminate random lag spikes in online games. (BBR remains available).
* **Boeffla Wakelock Blocker**: Natively injected into the kernel power management tree (`wakeup.c`). Allows precise blocking of rogue wakelocks to enforce deep sleep and drastically improve standby battery life.
* **WireGuard VPN**: Full native kernel-space WireGuard integration for zero-overhead, high-speed encrypted networking.

### 🥷 Military-Grade Stealth (Root)
* **Backslashxx KernelSU**: Integrated with the advanced [backslashxx](https://github.com/backslashxx) KernelSU fork.
* **Syscall Table Tampering (`CONFIG_KSU_TAMPER_SYSCALL_TABLE`)**: Bypasses standard tracepoint detection by surgically intercepting root requests directly in the assembly architecture.
* **SUSFS-Free**: Completely abandons legacy SUSFS architectures to avoid kernel panics and instability. Achieves a 100% "Green" pass on Native Detector and strict banking apps natively through Syscall Tampering.

---

## 🛠️ Installation

> [!WARNING]  
> Flashing custom kernels involves inherent risks. Ensure your bootloader is unlocked and you have a backup of your current `boot.img` before proceeding.

### Fastboot (Recommended)
1. Reboot your Nothing Phone (1) to the bootloader (`adb reboot bootloader`).
2. Flash the kernel image to both slots to ensure stability:
   ```bash
   fastboot flash boot_a Caly-Tachyon-boot.img
   fastboot flash boot_b Caly-Tachyon-boot.img
   ```
3. Reboot:
   ```bash
   fastboot reboot
   ```

---

## 🤝 Credits & Acknowledgements

* **NothingOSS**: For the base upstream kernel source.
* **[tiann](https://github.com/tiann) & [backslashxx](https://github.com/backslashxx)**: For their revolutionary work on KernelSU and Syscall Tampering.
* **[zx2c4](https://git.zx2c4.com/)**: For the wireguard-linux-compat integration.
* **The Android Custom Kernel Community**: For the continued development of WALT, Boeffla, and advanced schedulers.

<br>
<div align="center">
  <i>"Speed is not an option. It's a requirement."</i>
</div>
