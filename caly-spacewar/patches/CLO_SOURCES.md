# Upstream CLO 6.12 Sources & Surgical Bring-Up Patches

All Qualcomm / CodeLinaro driver repositories remain upstream under their
original Qualcomm / CodeLinaro licenses and copyrights. This directory stores
only the surgical bring-up diffs authored for Nothing Phone (1) (Spacewar / SM7325-AE).

## Upstream CLO 6.12 Base Commits (`LA.VENDOR.16.2.8.r1`)

- **display-drivers**: `https://git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers.git` @ `fe85946f41f9` -> `display-sde-vbif.patch`
- **graphics-kernel**: `https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/graphics-kernel.git` @ `1418e583ed18` -> `graphics-kgsl.patch`
- **audio-kernel-legacy**: `https://git.codelinaro.org/clo/la/platform/vendor/opensource/audio-kernel.git` @ `5a1060e1fb07` -> `audio-wcd938x-lahaina.patch`
- **dsp-kernel**: `https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/dsp-kernel.git` @ `92371be0b59e` -> `dsp-fastrpc-legacy.patch`
- **dataipa**: `https://git.codelinaro.org/clo/la/platform/vendor/opensource/dataipa.git` @ `392a1090bab9` -> `data-ipa.patch`
- **qca-wifi-host-cmn**: `https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qca-wifi-host-cmn.git` @ `d345f3d88f78` -> `wlan-cmn.patch`
- **wlan-platform**: `https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/platform.git` @ `f99ee306a725` -> `wlan-platform-icnss2.patch`
- **qcacld-3.0**: `https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qcacld-3.0.git` @ `0798a38ce6c4` -> `wlan-qcacld-wcn6750.patch`
- **qcom-soc (remoteproc q6v5)**: `https://git.codelinaro.org/clo/la/kernel/qcom.git` @ `46f13428ae2d` -> `rproc-q6v5-adsp.patch`
- **qcom-soc (msm_geni_serial)**: `https://git.codelinaro.org/clo/la/kernel/qcom.git` @ `46f13428ae2d` -> `uart-geni-bt.patch`
- **qcom-soc (dwc3-msm)**: `https://git.codelinaro.org/clo/la/kernel/qcom.git` @ `46f13428ae2d` -> `usb-dwc3-msm.patch`
- **qcom-soc (qti_battery_charger)**: `https://git.codelinaro.org/clo/la/kernel/qcom.git` @ `46f13428ae2d` -> `power-battery-usb-role.patch`
- **qcom-soc (qcom-lpm)**: `https://git.codelinaro.org/clo/la/kernel/qcom.git` @ `46f13428ae2d` -> `cpuidle-qcom-lpm.patch`

## Stock Spacewar 5.4 Vendor Modules Ported to Linux 6.12 APIs (Out-of-Tree)

Compiled cleanly against the `6.12.81-CALY-Base` GKI `Module.symvers` (`0` bad CRCs)
and shipped in `vendor_boot` ramdisk (see `caly-spacewar/packaging/anykernel3/modules.load`):
- **Spectra Camera ISP** (`camera.ko` - Qualcomm / NothingOSS Lahaina camera-kernel)
- **Iris2 HW Video Codec** (`msm_video.ko` - Qualcomm Lahaina video-driver, `UBSAN_SANITIZE=n`)
- **NXP TFA98xx Stereo SmartPA** (`tfa98xx_dlkm.ko` + `tfa98xx.cnt`)
- **QTI Flash LED** (`leds-qti-flash.ko`)
- **SLIMbus & BT/FM** (`slimbus.ko`, `slim_qcom_ngd_ctrl.ko`, `bt_fm_slim.ko`, `btpower.ko`)
- **SMCInvoke / QSEECOM** (`smcinvoke_dlkm.ko`, `qseecom_dlkm.ko`)
- **FocalTech FT3680 Touch** (`fts_tp.ko` -> `input-fts-dt2w.patch`), **Goodix UDFPS**, **AAC RichTap Haptics**
