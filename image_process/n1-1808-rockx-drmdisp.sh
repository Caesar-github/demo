cd /userdata/ && \
rk_image_process -input="/dev/video0" \
    -input_width 1920 -input_height 1080 -input_format 1 \
    -width 1280 -height 720 -format 3 -slice_num 1 \
    -disp -disp_rotate=90 \
    -processor="npu" -npu_data_source="none" -npu_model_name="dms" \
    -npu_piece_width 640 -npu_piece_height 360

# cd /userdata/ && \
# rk_image_process -input="/userdata/test_ssd.mp4" \
#    -width 1280 -height 720 -format 3 -slice_num 1 \
#    -disp -disp_rotate 90 \
#    -processor="npu" -npu_data_source="none" -npu_model_name="ssd" \
#    -loop
