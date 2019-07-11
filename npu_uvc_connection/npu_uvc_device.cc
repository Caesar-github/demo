/*
 * Copyright (C) 2019 Hertz Wang 1989wanghang@163.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 *
 * Any non-GPL usage of this software or parts of this software is strictly
 * forbidden.
 *
 */

// this file run on 1808 with npu
// data path: camera (-> mpp) -> rga -> npu ->uvc

#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef DEBUG
#define DEBUG
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <easymedia/buffer.h>
#include <easymedia/key_string.h>
#include <easymedia/media_config.h>
#include <easymedia/utils.h>

#include <easymedia/flow.h>

static std::shared_ptr<easymedia::Flow>
create_flow(const std::string &flow_name, const std::string &flow_param,
            const std::string &elem_param);

extern "C" {
#include <uvc/mpi_enc.h>
#include <uvc/uvc_control.h>
#include <uvc/uvc_video.h>
}
#include "npu_uvc_shared.h"
#include <rknn_runtime.h>
static bool do_uvc(easymedia::Flow *f,
                   easymedia::MediaBufferVector &input_vector);
class UVCJoinFlow : public easymedia::Flow {
public:
  UVCJoinFlow(const char *param);
  virtual ~UVCJoinFlow() {
    StopAllThread();
    uvc_video_id_exit_all();
  }
  bool Init();

private:
  friend bool do_uvc(easymedia::Flow *f,
                     easymedia::MediaBufferVector &input_vector);
};

UVCJoinFlow::UVCJoinFlow(const char *param _UNUSED) {
  easymedia::SlotMap sm;
  sm.thread_model = easymedia::Model::ASYNCCOMMON;
  sm.mode_when_full = easymedia::InputMode::DROPFRONT;
  sm.input_slots.push_back(0);
  sm.input_maxcachenum.push_back(2);
  sm.fetch_block.push_back(true);
  sm.input_slots.push_back(1);
  sm.input_maxcachenum.push_back(1);
  sm.fetch_block.push_back(false);
  sm.process = do_uvc;
  if (!InstallSlotMap(sm, "uvc", -1)) {
    LOG("Fail to InstallSlotMap, %s\n", "uvc_join");
    SetError(-EINVAL);
    return;
  }
}

bool UVCJoinFlow::Init() {
  if (!check_uvc_video_id()) {
    add_uvc_video();
    return true;
  }
  fprintf(stderr, "not uvc video support\n");
  return false;
}

static MppFrameFormat ConvertToMppPixFmt(const PixelFormat &fmt) {
  static_assert(PIX_FMT_YUV420P == 0, "The index should greater than 0\n");
  static MppFrameFormat mpp_fmts[PIX_FMT_NB] = {
      [PIX_FMT_YUV420P] = MPP_FMT_YUV420P,
      [PIX_FMT_NV12] = MPP_FMT_YUV420SP,
      [PIX_FMT_NV21] = MPP_FMT_YUV420SP_VU,
      [PIX_FMT_YUV422P] = MPP_FMT_YUV422P,
      [PIX_FMT_NV16] = MPP_FMT_YUV422SP,
      [PIX_FMT_NV61] = MPP_FMT_YUV422SP_VU,
      [PIX_FMT_YUYV422] = MPP_FMT_YUV422_YUYV,
      [PIX_FMT_UYVY422] = MPP_FMT_YUV422_UYVY,
      [PIX_FMT_RGB332] = (MppFrameFormat)-1,
      [PIX_FMT_RGB565] = MPP_FMT_RGB565,
      [PIX_FMT_BGR565] = MPP_FMT_BGR565,
      [PIX_FMT_RGB888] = MPP_FMT_RGB888,
      [PIX_FMT_BGR888] = MPP_FMT_BGR888,
      [PIX_FMT_ARGB8888] = MPP_FMT_ARGB8888,
      [PIX_FMT_ABGR8888] = MPP_FMT_ABGR8888};
  if (fmt >= 0 && fmt < PIX_FMT_NB)
    return mpp_fmts[fmt];
  return (MppFrameFormat)-1;
}

bool do_uvc(easymedia::Flow *f _UNUSED,
            easymedia::MediaBufferVector &input_vector) {
  auto img_buf = input_vector[0];
  auto &npu_output_buf = input_vector[1];
  if (!img_buf || img_buf->GetType() != Type::Image)
    return false;
  auto img = std::static_pointer_cast<easymedia::ImageBuffer>(img_buf);
  MppFrameFormat ifmt = ConvertToMppPixFmt(img->GetPixelFormat());
  // printf("ifmt: %d, size: %d\n", ifmt, (int)img_buf->GetValidSize());
  if (ifmt < 0)
    return false;
  mpi_enc_set_format(ifmt);
  struct extra_jpeg_data ejd;
  ejd.picture_timestamp = img_buf->GetTimeStamp();
  if (!npu_output_buf) {
    ejd.npu_outputs_num = 0;
    ejd.npu_outputs_timestamp = 0;
    ejd.npu_output_size = 0;
    uvc_read_camera_buffer(img_buf->GetPtr(), img_buf->GetValidSize(), &ejd,
                           sizeof(ejd));
  } else {
    size_t num = npu_output_buf->GetValidSize();
    rknn_output *outputs = (rknn_output *)npu_output_buf->GetPtr();
    ejd.npu_outputs_num = num;
    ejd.npu_outputs_timestamp = npu_output_buf->GetTimeStamp();
    ejd.npu_output_size = num * sizeof(struct aligned_npu_output);
    for (size_t i = 0; i < num; i++)
      ejd.npu_output_size += outputs[i].size;
    size_t size = sizeof(ejd) + ejd.npu_output_size;
    struct extra_jpeg_data *new_ejd = (struct extra_jpeg_data *)malloc(size);
    if (!new_ejd)
      return false;
    *new_ejd = ejd;
    struct aligned_npu_output *an =
        (struct aligned_npu_output *)new_ejd->outputs;
    for (size_t i = 0; i < num; i++) {
      an[i].want_float = outputs[i].want_float;
      an[i].is_prealloc = outputs[i].is_prealloc;
      an[i].index = outputs[i].index;
      an[i].size = outputs[i].size;
      memcpy(an[i].buf, outputs[i].buf, outputs[i].size);
    }
    uvc_read_camera_buffer(img_buf->GetPtr(), img_buf->GetValidSize(), new_ejd,
                           size);
    free(new_ejd);
  }
  return true;
}

static char optstr[] = "?i:w:h:f:m:n:";

int main(int argc, char **argv) {
  int c;
  std::string v4l2_video_path;
  std::string format = IMAGE_NV12;
  int width = 1280, height = 720;
  int npu_width = 300, npu_height = 300;
  std::string model_path;
  std::string model_name;

  opterr = 1;
  while ((c = getopt(argc, argv, optstr)) != -1) {
    switch (c) {
    case 'i':
      v4l2_video_path = optarg;
      printf("input path: %s\n", v4l2_video_path.c_str());
      break;
    case 'f':
      format = optarg;
      printf("input format: %s\n", format.c_str());
      break;
    case 'w':
      width = atoi(optarg);
      break;
    case 'h':
      height = atoi(optarg);
      break;
    case 'm':
      model_path = optarg;
      printf("model path: %s\n", model_path.c_str());
      break;
    case 'n': {
      char *s = strchr(optarg, ':');
      assert(s);
      *s = 0;
      model_name = optarg;
      printf("model name: %s\n", model_name.c_str());
      int ret = sscanf(s + 1, "%dx%d\n", &npu_width, &npu_height);
      assert(ret == 2);
    } break;
    case '?':
    default:
      printf("help:\n\t-i: v4l2 capture device path\n");
      printf("\t-f: v4l2 capture format\n");
      printf("\t-w: v4l2 capture width\n");
      printf("\t-h: v4l2 capture height\n");
      printf("\t-m: model path\n");
      printf("\t-n: model name, model request width/height; such as "
             "ssd:300x300\n");
      printf("\nusage example: \n");
      printf("npu_uvc_device -i /dev/video6 -f image:jpeg -w 1280 -h 720 -m "
             "/userdata/ssd_inception_v2.rknn -n ssd\n");
      exit(0);
    }
  }
  assert(!v4l2_video_path.empty());
  assert(!model_path.empty());

  // mpp
  std::shared_ptr<easymedia::Flow> decoder;
  // many usb camera decode out fmt do not match to rkmpp,
  // rga do convert
  std::shared_ptr<easymedia::Flow> rga0;
  if (format == IMAGE_JPEG || format == VIDEO_H264) {
    std::string flow_name("video_dec");
    std::string codec_name("rkmpp");
    std::string flow_param;
    PARAM_STRING_APPEND(flow_param, KEY_NAME, codec_name);
    std::string dec_param;
    PARAM_STRING_APPEND(dec_param, KEY_INPUTDATATYPE, format);
    // set output data type work only for jpeg, but except 1808
    // PARAM_STRING_APPEND(dec_param, KEY_OUTPUTDATATYPE, IMAGE_NV12);
    PARAM_STRING_APPEND_TO(dec_param, KEY_OUTPUT_TIMEOUT, 5000);
    decoder = create_flow(flow_name, flow_param, dec_param);
    if (!decoder) {
      assert(0);
      return -1;
    }
  }

  if (decoder) {
    std::string flow_name("filter");
    std::string filter_name("rkrga");
    std::string flow_param;
    PARAM_STRING_APPEND(flow_param, KEY_NAME, filter_name);
    PARAM_STRING_APPEND(flow_param, KEK_THREAD_SYNC_MODEL, KEY_ASYNCCOMMON);
    PixelFormat rga_out_pix_fmt = GetPixFmtByString(IMAGE_NV12);
    ImageInfo out_img_info = {rga_out_pix_fmt, 0, 0, 0, 0};
    flow_param.append(easymedia::to_param_string(out_img_info, false));
    std::string rga_param;
    std::vector<ImageRect> v = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    PARAM_STRING_APPEND(rga_param, KEY_BUFFER_RECT,
                        easymedia::TwoImageRectToString(v));
    rga0 = create_flow(flow_name, flow_param, rga_param);
    if (!rga0) {
      assert(0);
      return -1;
    }
  }

  // rga
  std::shared_ptr<easymedia::Flow> rga;
  do {
    std::string flow_name("filter");
    std::string filter_name("rkrga");
    std::string flow_param;
    PARAM_STRING_APPEND(flow_param, KEY_NAME, filter_name);
    PARAM_STRING_APPEND(flow_param, KEK_THREAD_SYNC_MODEL, KEY_ASYNCCOMMON);
    PixelFormat rga_out_pix_fmt = GetPixFmtByString(IMAGE_RGB888);
    ImageInfo out_img_info = {rga_out_pix_fmt, npu_width, npu_height, npu_width,
                              npu_height};
    if (!decoder)
      PARAM_STRING_APPEND(flow_param, KEY_INPUTDATATYPE, format);
    flow_param.append(easymedia::to_param_string(out_img_info, false));
    std::string rga_param;
    std::vector<ImageRect> v = {{0, 0, width, height},
                                {0, 0, npu_width, npu_height}};
    PARAM_STRING_APPEND(rga_param, KEY_BUFFER_RECT,
                        easymedia::TwoImageRectToString(v));
    rga = create_flow(flow_name, flow_param, rga_param);
    if (!rga) {
      assert(0);
      return -1;
    }
  } while (0);

  // rknn
  std::shared_ptr<easymedia::Flow> rknn;
  do {
    std::string flow_name("filter");
    std::string filter_name("rknn");
    std::string flow_param;
    PARAM_STRING_APPEND(flow_param, KEY_NAME, filter_name);
    PARAM_STRING_APPEND(flow_param, KEK_THREAD_SYNC_MODEL, KEY_ASYNCCOMMON);
    PARAM_STRING_APPEND(flow_param, KEY_INPUTDATATYPE, IMAGE_RGB888);
    std::string rknn_param;
    PARAM_STRING_APPEND(rknn_param, KEY_PATH, model_path);
    std::string str_tensor_type;
    std::string str_tensor_fmt;
    std::string str_want_float;
    if (model_name == "ssd") {
      str_tensor_type = NN_UINT8;
      str_tensor_fmt = KEY_NHWC;
      str_want_float = "1,1";
    } else {
      fprintf(stderr, "TODO for %s\n", model_name.c_str());
      assert(0);
      return -1;
    }
    PARAM_STRING_APPEND(rknn_param, KEY_TENSOR_TYPE, str_tensor_type);
    PARAM_STRING_APPEND(rknn_param, KEY_TENSOR_FMT, str_tensor_fmt);
    PARAM_STRING_APPEND(rknn_param, KEY_OUTPUT_WANT_FLOAT, str_want_float);
    rknn = create_flow(flow_name, flow_param, rknn_param);
    if (!rknn) {
      assert(0);
      return -1;
    }
    rga->AddDownFlow(rknn, 0, 0);
  } while (0);

  // uvc
  auto uvc = std::make_shared<UVCJoinFlow>(nullptr);
  if (!uvc || uvc->GetError() || !uvc->Init()) {
    fprintf(stderr, "Fail to create uvc\n");
    return -1;
  }
  rknn->AddDownFlow(uvc, 0, 1);

  // finally, create v4l2 flow
  std::shared_ptr<easymedia::Flow> v4l2_flow;
  do {
    std::string flow_name("source_stream");
    std::string stream_name("v4l2_capture_stream");
    std::string flow_param;
    PARAM_STRING_APPEND(flow_param, KEY_NAME, stream_name);
    std::string v4l2_param;
    PARAM_STRING_APPEND_TO(v4l2_param, KEY_USE_LIBV4L2, 1);
    PARAM_STRING_APPEND(v4l2_param, KEY_DEVICE, v4l2_video_path);
    PARAM_STRING_APPEND(v4l2_param, KEY_V4L2_CAP_TYPE,
                        KEY_V4L2_C_TYPE(VIDEO_CAPTURE));
    PARAM_STRING_APPEND(v4l2_param, KEY_V4L2_MEM_TYPE,
                        KEY_V4L2_M_TYPE(MEMORY_MMAP));
    PARAM_STRING_APPEND_TO(v4l2_param, KEY_FRAMES, 4);
    PARAM_STRING_APPEND(v4l2_param, KEY_OUTPUTDATATYPE, format);
    PARAM_STRING_APPEND_TO(v4l2_param, KEY_BUFFER_WIDTH, width);
    PARAM_STRING_APPEND_TO(v4l2_param, KEY_BUFFER_HEIGHT, height);
    v4l2_flow = create_flow(flow_name, flow_param, v4l2_param);
    if (!v4l2_flow) {
      assert(0);
      return -1;
    }
    if (decoder) {
      decoder->AddDownFlow(rga0, 0, 0);
      decoder->AddDownFlow(rga, 0, 0);
      rga0->AddDownFlow(uvc, 0, 0);
      v4l2_flow->AddDownFlow(decoder, 0, 0);
    } else {
      v4l2_flow->AddDownFlow(rga, 0, 0);
      v4l2_flow->AddDownFlow(uvc, 0, 0);
    }
  } while (0);

  while (getchar() != 'q')
    easymedia::msleep(10);

#if 1
  if (decoder) {
    v4l2_flow->RemoveDownFlow(decoder);
    v4l2_flow.reset();
    decoder->RemoveDownFlow(rga);
    decoder->RemoveDownFlow(rga0);
    decoder.reset();
    rga0->RemoveDownFlow(uvc);
    rga0.reset();
  } else {
    v4l2_flow->RemoveDownFlow(rga);
    v4l2_flow->RemoveDownFlow(uvc);
    v4l2_flow.reset();
  }
  rga->RemoveDownFlow(rknn);
  rga.reset();
  rknn->RemoveDownFlow(uvc);
  uvc.reset();
  rknn.reset();
#endif

  return 0;
}

std::shared_ptr<easymedia::Flow> create_flow(const std::string &flow_name,
                                             const std::string &flow_param,
                                             const std::string &elem_param) {
  auto &&param = easymedia::JoinFlowParam(flow_param, 1, elem_param);
  auto ret = easymedia::REFLECTOR(Flow)::Create<easymedia::Flow>(
      flow_name.c_str(), param.c_str());
  if (!ret)
    fprintf(stderr, "Create flow %s failed\n", flow_name.c_str());
  return ret;
}
