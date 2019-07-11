#!/bin/bash
# call this file on 1808

for dev in `ls /dev/video*`
do
    v4l2-ctl -d $dev -D | grep rkisp1_mainpath > /dev/null
    if [ $? -eq 0 -a -z "$rkisp_main" ]; then rkisp_main=$dev; fi
    v4l2-ctl -d $dev -D | grep uvcvideo > /dev/null
    if [ $? -eq 0 -a -z "$uvcvideo" ]; then uvcvideo=$dev; fi
    v4l2-ctl -d $dev -D | grep cif > /dev/null
    if [ $? -eq 0 -a -z "$cifvideo" ]; then cifvideo=$dev; fi
done
# uvc first, then rkisp
echo uvcvideo=$uvcvideo,rkisp_main=$rkisp_main,cifvideo=$cifvideo

if test $rkisp_main; then
    input=$rkisp_main
    input_format=image:nv12
elif test $uvcvideo; then
    input=$uvcvideo
    input_format=image:jpeg
elif test $cifvideo; then
    input=$cifvideo
    input_format=image:nv16
else
    echo find non valid camera via v4l2-ctl
fi
#input_format=image:yuv420p
uvc_MJPEG.sh 1280 720
rk_npu_uvc_device -i $input -f $input_format -w 1280 -h 720 \
        -m /userdata/ssd_inception_v2.rknn -n ssd:300x300
