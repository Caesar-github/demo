#!/bin/bash

# call this on 3399

input=/dev/video0

# PC: adb push external/rknpu/rknn/rknn_api/examples/rknn_ssd_demo/model/box_priors.txt /usr/bin/
# PC: adb push external/rknpu/rknn/rknn_api/examples/rknn_ssd_demo/model/coco_labels_list.txt /usr/bin/
rk_npu_uvc_host -i $input -w 1280 -h 720
