#!/bin/bash
#
# kimocoder kernel builder and packer
# 2024 - kimocoder
#

# Setup getopt.
long_opts="regen,clean,homedir:,tcdir:"
getopt_cmd=$(getopt -o rch:t: --long "$long_opts" \
            -n $(basename $0) -- "$@") || \
            { echo -e "\nError: Getopt failed. Extra args\n"; exit 1;}

eval set -- "$getopt_cmd"

while true; do
    case "$1" in
        -r|--regen|r|regen) FLAG_REGEN_DEFCONFIG=y;;
        -c|--clean|c|clean) FLAG_CLEAN_BUILD=y;;
        -h|--homedir|h|homedir) HOME_DIR="$2"; shift;;
        -t|--tcdir|t|tcdir) TC_DIR="$2"; shift;;
        --) shift; break;;
    esac
    shift
done

# Setup directories
HOME_DIR="${HOME_DIR:-$HOME}"
echo -e "HOME directory is at $HOME_DIR\n"

if [ -n "$TC_DIR" ] && [[ "$TC_DIR" != /* ]]; then
     TC_DIR="$HOME_DIR/$TC_DIR"
elif [ -z "$TC_DIR" ]; then
    TC_DIR="$HOME_DIR/tc"
fi
echo -e "Toolchain directory is at $TC_DIR\n"

SECONDS=0 # builtin bash timer
ZIPNAME="Uo_Spacewar_NOS3.0_Kernel.zip"

CLANG_DIR="$TC_DIR/r383902b1"
AK3_DIR="$HOME_DIR/AnyKernel3"
DEFCONFIG="spacewar_defconfig"

export PATH="$CLANG_DIR/bin:$PATH"

# Enable ccache if available
if command -v ccache &> /dev/null; then
    echo -e "ccache found! Enabling build caching...\n"
    export CCACHE_COMPRESS=1
    export CCACHE_DIR="$HOME_DIR/.ccache"
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
)

# Regenerate defconfig, if requested so
if [ "$FLAG_REGEN_DEFCONFIG" = 'y' ]; then
	make "${MAKE_PARAMS[@]}" "$DEFCONFIG" savedefconfig
	cp out/defconfig "arch/arm64/configs/$DEFCONFIG"
	echo -e "\nSuccessfully regenerated defconfig at $DEFCONFIG"
	exit 0
fi

# Prep for a clean build, if requested so
if [ "$FLAG_CLEAN_BUILD" = 'y' ]; then
	echo -e "\nCleaning output folder..."
	rm -rf out
fi

mkdir -p out
make "${MAKE_PARAMS[@]}" "$DEFCONFIG"

echo -e "\nStarting compilation...\n"
make -j$(nproc --all) "${MAKE_PARAMS[@]}" || { echo -e "\nCompilation failed!"; exit 1; }
make -j$(nproc --all) "${MAKE_PARAMS[@]}" INSTALL_MOD_PATH=modules INSTALL_MOD_STRIP=1 modules_install

kernel="out/arch/arm64/boot/Image"
dts_dir="out/arch/arm64/boot/dts/vendor/qcom"

if [ -f "$kernel" ] && [ -d "$dts_dir" ]; then
	echo -e "\nKernel compiled successfully! Zipping up...\n"
	if [ -d "$AK3_DIR" ]; then
		cp -r "$AK3_DIR" AnyKernel3
		cd AnyKernel3 || exit 1
		git checkout spacewar_nos3.0 &> /dev/null || git checkout spacewar &> /dev/null
		cd ..
	elif ! git clone https://github.com/zerofrip/AnyKernel3 -b spacewar_nos3.0; then
		echo -e "\nAnyKernel3 repo not found locally and couldn't clone from GitHub! Aborting..."
		exit 1
	fi
	cp "$kernel" AnyKernel3/
	cat "$dts_dir"/*.dtb > AnyKernel3/dtb
	if ls "$dts_dir"/*.dtbo >/dev/null 2>&1; then
		python3 scripts/mkdtboimg.py create AnyKernel3/dtbo.img --page_size=4096 "$dts_dir"/*.dtbo
	fi
	rm -rf out/arch/arm64/boot
	cd AnyKernel3 || exit 1
	zip -r9 "../$ZIPNAME" ./* -x .git README.md '*placeholder*'
	cd ..
	rm -rf AnyKernel3
	echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s)!"
	echo "Zip: $ZIPNAME"
else
	echo -e "\nCompilation failed! Missing Kernel Image or DTB files."
	exit 1
fi
