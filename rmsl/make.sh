GCC=/home/zsq/29/linux-sdk/buildroot/output/rockchip_rk3326_64/host/bin/aarch64-buildroot-linux-gnu-gcc
SYSROOT=/home/zsq/29/linux-sdk/buildroot/output/rockchip_rk3326_64/staging
$GCC --sysroot=$SYSROOT \
	main.c rmsl_ctrl.c vpu_decode.c rmsl_depth.c rkdrm_display.c \
	-lrkisp -lrkisp_api -lrockchip_mpp -lrga -ldrm \
	-I./ \
	-I${SYSROOT}/usr/include/libdrm/ \
	-Werror -Wall -g -o rmsl_linux_demo

$GCC --sysroot=$SYSROOT \
	rmsl_tool.c rmsl_ctrl.c \
	-I./ \
	-Werror -Wall -o rmsl_tool

GCC=/home/zsq/29/linux-out/buildroot/output/rockchip_rk3288/host/bin/arm-buildroot-linux-gnueabihf-gcc
SYSROOT=/home/zsq/29/linux-out/buildroot/output/rockchip_rk3288/staging

$GCC --sysroot=$SYSROOT \
	main.c rmsl_ctrl.c vpu_decode.c rmsl_depth.c rkdrm_display.c \
	-lrkisp -lrkisp_api -lrockchip_mpp -lrga -ldrm \
	-I./ \
	-I${SYSROOT}/usr/include/libdrm/ \
	-g -o rmsl_linux_demo_armhf

$GCC --sysroot=$SYSROOT \
	rmsl_tool.c rmsl_ctrl.c \
	-I./ \
	-o rmsl_tool_armhf

