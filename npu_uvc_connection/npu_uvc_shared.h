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

#ifndef NPU_UVC_SHARED_H_
#define NPU_UVC_SHARED_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct aligned_npu_output {
  uint8_t want_float;
  uint8_t is_prealloc;
  uint32_t index;
  uint32_t size;
  uint8_t buf[0];
} __attribute__((packed));

struct extra_jpeg_data {
  int64_t picture_timestamp;     // the time stamp of picture
  uint32_t npu_outputs_num;      // the num of npu output
  int64_t npu_outputs_timestamp; // the time stamp of npu outputs
  uint32_t npu_output_size;      // the size of all aligned_npu_output
  uint8_t outputs[0];            // the buffer of npu outputs
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif
