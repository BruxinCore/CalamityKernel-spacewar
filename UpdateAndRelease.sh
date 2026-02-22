#!/bin/bash
#
# Automated Update, Build, Repack and Release Script
# Optimized for forked repositories where GitHub Actions are unavailable.
#

# --- Configuration & Dependency Check ---
TC_DIR="${HOME}/tc"
CLANG_DIR="$TC_DIR/r383902b1"
BOOT_EDITOR_DIR="$TC_DIR/Android_boot_image_editor"
AK3_DIR="${HOME}/AnyKernel3"
DEFCONFIG="spacewar_defconfig"

# Warning messages for missing dependencies
if [ ! -d "$CLANG_DIR" ]; then
    echo "⚠️  CRITICAL ERROR: Clang toolchain not found at $CLANG_DIR"
    echo "Please download it from: https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/refs/heads/master/clang-r383902b1/"
    echo "And extract it to your toolchain directory."
    exit 1
fi

if [ ! -d "$BOOT_EDITOR_DIR" ]; then
    echo "⚠️  CRITICAL ERROR: Android_boot_image_editor not found at $BOOT_EDITOR_DIR"
    echo "Please clone it: git clone https://github.com/cfig/Android_boot_image_editor.git"
    echo "And place it in your toolchain directory."
    exit 1
fi

# Ensure we are in the kernel root
if [ ! -f "Kbuild" ]; then
    echo "❌ Error: Please run this script from the kernel root directory."
    exit 1
fi

SECONDS=0
export PATH="$CLANG_DIR/bin:$PATH"

# --- 1. Update KernelSU-Next & SUSFS ---
echo "💉 Updating KernelSU-Next & SUSFS..."

# Sync KernelSU-Next fork with upstream
echo "🔄 Syncing KernelSU-Next fork with upstream..."
pushd KernelSU-Next > /dev/null
git reset --hard HEAD
git clean -fd
git remote add upstream https://github.com/KernelSU-Next/KernelSU-Next.git 2>/dev/null
git fetch upstream dev_susfs

if git diff --quiet HEAD upstream/dev_susfs; then
    echo "✅ KernelSU-Next fork is up to date with upstream."
else
    echo "🚀 KernelSU-Next upstream updates detected! Merging into fork..."
    git checkout dev_susfs
    git merge upstream/dev_susfs --no-edit
    git push origin dev_susfs
fi
popd > /dev/null

# Update submodules to latest
echo "📦 Updating submodules..."
git submodule update --init --recursive
if [ -f "KernelSU-Next-5.4-compat.patch" ]; then
    echo "🩹 Applying 5.4 compatibility patch to KernelSU-Next..."
    pushd KernelSU-Next > /dev/null
    git reset --hard HEAD
    patch -p1 < ../KernelSU-Next-5.4-compat.patch
    popd > /dev/null
fi

# Download SUSFS headers/core
git clone --depth 1 -b gki-android12-5.10 https://gitlab.com/simonpunk/susfs4ksu.git temp_susfs4ksu
cp -v temp_susfs4ksu/kernel_patches/include/linux/susfs*.h include/linux/
if [ -f "temp_susfs4ksu/kernel_patches/include/linux/mac_user_macros.h" ]; then
    cp -v temp_susfs4ksu/kernel_patches/include/linux/mac_user_macros.h include/linux/
fi
cp -v temp_susfs4ksu/kernel_patches/fs/susfs.c fs/susfs.c
rm -rf temp_susfs4ksu

# Apply 5.4 compat injections
python3 -c '
file_path = "fs/susfs.c"
with open(file_path, "r") as f:
    text = f.read()
legacy_ops = """static const struct fsnotify_ops fsnotify_ops = {
	.handle_inode_event = susfs_handle_sdcard_inode_event,
};"""
compat_wrapper = """#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static const struct fsnotify_ops fsnotify_ops = {
	.handle_inode_event = susfs_handle_sdcard_inode_event,
};
#else
static int susfs_handle_sdcard_event_compat(struct fsnotify_group *group, struct inode *inode, u32 mask, const void *data, int data_type, const struct qstr *file_name, u32 cookie, struct fsnotify_iter_info *iter_info)
{
    return susfs_handle_sdcard_inode_event(NULL, mask, inode, NULL, file_name, cookie);
}
static const struct fsnotify_ops fsnotify_ops = {
	.handle_event = susfs_handle_sdcard_event_compat,
};
#endif"""
if legacy_ops in text:
    text = text.replace(legacy_ops, compat_wrapper)
    text = text.replace("extern bool susfs_is_avc_log_spoofing_enabled;", "bool susfs_is_avc_log_spoofing_enabled = false;")
    with open(file_path, "w") as f:
        f.write(text)
    print("Successfully injected 5.4 compat wrapper into susfs.c")
'

# --- 2. Build Kernel ---
echo "🏗 Starting Kernel Build..."

# Read version info
VERSION=$(cat VERSION | tr -d '\n')
PATCHLEVEL=$(cat PATCHLEVEL 2>/dev/null | tr -d '\n' || echo "0")
KVER="-Spacewar-v${VERSION}.${PATCHLEVEL}"
FULL_VERSION="v${VERSION}.${PATCHLEVEL}"

# Enable ccache if available
if command -v ccache &> /dev/null; then
    echo -e "ccache found! Enabling build caching...\n"
    export CCACHE_COMPRESS=1
    export CCACHE_DIR="${HOME}/.ccache"
    export CCACHE_MAXSIZE="50G"
    CC_CMD="ccache clang"
else
    echo -e "ccache not found! Proceeding without cache...\n"
    CC_CMD="clang"
fi

MAKE_PARAMS=(
    O=out
    ARCH=arm64
    CC="$CC_CMD"
    CLANG_TRIPLE=aarch64-linux-gnu-
    LLVM=1
    LLVM_IAS=1
    CROSS_COMPILE="$CLANG_DIR/bin/llvm-"
    LOCALVERSION="$KVER"
)

mkdir -p out
make "${MAKE_PARAMS[@]}" "$DEFCONFIG"
make -j$(nproc --all) "${MAKE_PARAMS[@]}" || { echo "❌ Build Failed!"; exit 1; }

# --- 3. Repack boot.img ---
echo "📦 Repacking boot.img..."
# Fetch latest Spacewar stock boot.img
echo "🔍 Searching for latest Spacewar boot image..."
LATEST_TAG=$(curl -s https://api.github.com/repos/spike0en/nothing_archive/releases | grep -oP '"tag_name": "\KSpacewar_[^"]+' | head -n 1)

if [ -z "$LATEST_TAG" ]; then
    echo "⚠️  Failed to fetch latest tag, using fallback..."
    LATEST_TAG="Spacewar_V3.2-251219-1652"
fi

echo "📦 Downloading stock boot image from tag: $LATEST_TAG"
DOWNLOAD_URL="https://github.com/spike0en/nothing_archive/releases/download/${LATEST_TAG}/${LATEST_TAG}-image-boot.7z"
curl -sL "$DOWNLOAD_URL" -o image-boot.7z

if [ ! -f "image-boot.7z" ] || [ ! -s "image-boot.7z" ]; then
    echo "❌ Error: Download failed or file is empty."
    if [ "$LATEST_TAG" != "Spacewar_V3.2-251219-1652" ]; then
        LATEST_TAG="Spacewar_V3.2-251219-1652"
        DOWNLOAD_URL="https://github.com/spike0en/nothing_archive/releases/download/${LATEST_TAG}/${LATEST_TAG}-image-boot.7z"
        echo "🔄 Retrying with fallback URL: $DOWNLOAD_URL"
        curl -sL "$DOWNLOAD_URL" -o image-boot.7z
    fi
fi

if [ -f "image-boot.7z" ]; then
    7z x image-boot.7z
    find . -name "boot.img" -exec mv {} ./boot.img \;
    rm image-boot.7z
else
    echo "❌ Error: Could not obtain image-boot.7z. Repacking will be skipped."
    exit 1
fi

# Use local Android_boot_image_editor
cp boot.img "$BOOT_EDITOR_DIR/"
pushd "$BOOT_EDITOR_DIR" > /dev/null
./gradlew unpack
cp ../../Spacewar_NOS3.0_Kernel/out/arch/arm64/boot/Image build/unzip_boot/kernel
./gradlew pack
cp boot.img.signed ../../Spacewar_NOS3.0_Kernel/boot.img
popd > /dev/null

# --- 4. Finalize ---
DATE=$(date +'%Y%m%d%H%M')
DATE_DISPLAY=$(date +'%Y-%m-%d %H:%M')
ZIP_NAME="Uo_Spacewar_NOS3.0_Kernel_${FULL_VERSION}_${DATE}.zip"

# Create AnyKernel3 ZIP (logic from build.sh)
if [ ! -d "AnyKernel3" ]; then
    git clone https://github.com/zerofrip/AnyKernel3 -b spacewar_nos3.0
fi
cp out/arch/arm64/boot/Image AnyKernel3/
cat out/arch/arm64/boot/dts/vendor/qcom/*.dtb > AnyKernel3/dtb
if ls out/arch/arm64/boot/dts/vendor/qcom/*.dtbo >/dev/null 2>&1; then
    python3 scripts/mkdtboimg.py create AnyKernel3/dtbo.img --page_size=4096 out/arch/arm64/boot/dts/vendor/qcom/*.dtbo
fi
cd AnyKernel3
zip -r9 "../$ZIP_NAME" ./* -x .git README.md '*placeholder*'
cd ..

# Update ChangeLog.txt
echo -e "Unofficial NOS3.0 Spacewar Kernel ${FULL_VERSION}\nDate: ${DATE_DISPLAY}\n- Automated build\n- Updated KernelSU-Next & SUSFS v2.0.0\n\n$(cat ChangeLog.txt)" > ChangeLog.txt

# Update metadata JSON
ZIP_SHA1=$(sha1sum "$ZIP_NAME" | awk '{print $1}')
jq --arg date "$(date +'%Y-%m-%d')" --arg version "5.4.289-${FULL_VERSION}" --arg sha1 "$ZIP_SHA1" \
'.kernel.date = $date | .kernel.version = $version | .kernel.sha1 = $sha1' kernel-downloads.json > temp.json
mv temp.json kernel-downloads.json

echo "✅ DONE! Build completed in $((SECONDS / 60))m $((SECONDS % 60))s"
echo "Artifacts:"
echo "  - $ZIP_NAME"
echo "  - boot.img"
echo "  - ChangeLog.txt (Updated)"
echo "  - kernel-downloads.json (Updated)"

# Optional: Commit changes
git add kernel-downloads.json ChangeLog.txt VERSION PATCHLEVEL
git commit -m "chore: Update metadata for version ${FULL_VERSION}"
