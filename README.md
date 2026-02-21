# 🌌 Unofficial NOS 3.0 Spacewar Kernel

![Build Status](https://img.shields.io/github/actions/workflow/status/zerofrip/Spacewar_NOS3.0_Kernel/update_susfs.yml?branch=sm7325/v/mr&label=Build&style=for-the-badge)
![KernelSU-Next](https://img.shields.io/badge/KernelSU--Next-Integrated-blueviolet?style=for-the-badge)
![SUSFS](https://img.shields.io/badge/SUSFS-v2.0.0-orange?style=for-the-badge)

Nothing Phone (1) (**spacewar**) 向けの、最新バイパス機能を盛り込んだ非公式 NOS 3.0 カーネルです。

---

## 🚀 主な機能

- **KernelSU-Next**: [rifsxd氏](https://github.com/rifsxd/KernelSU-Next)による最新の `dev_susfs` ブランチを採用。
- **SUSFS v2.0.0**: 5.4カーネル向けに最適化。高度なディレクトリ隠し、マウント隠しをサポート。
- **WireGuard**: `wireguard-linux-compat` 搭載。高速でセキュアなVPN接続が可能。
- **CI/CD 自動化**: 毎月、NothingOSSの上流更新をチェックし、最新パッチを当てて自動ビルド＆リリースを行います。

---

## 🛠 インストール方法

### 1. boot.img を使用する場合
[Releases](https://github.com/zerofrip/Spacewar_NOS3.0_Kernel/releases) から最新の `boot.img` をダウンロードします。
```bash
# ブートローダーから書き込み
fastboot flash boot boot.img
```

### 2. Kernel Manager を使用する場合
[Releases](https://github.com/zerofrip/Spacewar_NOS3.0_Kernel/releases) から `Uo_Spacewar_NOS3.0_Kernel_*.zip` をダウンロードし、Kernel Manager 等でフラッシュしてください。

---

## 🔄 アップデート通知設定

[Franco Kernel Manager](https://play.google.com/store/apps/details?id=com.franco.kernel) 等で以下のURLを登録すると、アプリ内で自動更新通知を受け取れます。

```text
https://raw.githubusercontent.com/zerofrip/Spacewar_NOS3.0_Kernel/refs/heads/sm7325/v/mr/kernel-downloads.json
```

---

## 🤝 クレジット & 感謝

- **Upstream Kernel**: [NothingOSS](https://github.com/NothingOSS/android_kernel_msm-5.4_nothing_sm7325/tree/sm7325/v/mr)
- **Reference Kernel**: [Christian <kimo> B.](https://github.com/kimocoder/android_kernel_nothing_sm7325)
- **KernelSU-Next**: [rifsxd](https://github.com/rifsxd/KernelSU-Next)
- **SUSFS**: [simonpunk](https://gitlab.com/simonpunk/susfs4ksu)
- **Boot Image Tool**: [Android_boot_image_editor](https://github.com/cfig/Android_boot_image_editor)
- **Stock Images**: [spike0en/nothing_archive](https://github.com/spike0en/nothing_archive)
- **Tester**: `⫷t⫸⫷u⫸⫷y⫸⫷i⫸⫷e⫸`

---

> [!NOTE]
> このカーネルの使用は自己責任でお願いします。ブートローダーがアンロックされていることが前提です。
