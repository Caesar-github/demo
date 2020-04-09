#ifndef RMSL_API_H__
#define RMSL_API_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RMSL_DATA_SIZE_CMD 0x04
#define RMSL_DATA_SIZE_QUERY 0x3c

int rmsl_get_version(int fd, char *ver, int size);
int rmsl_get_sn(int fd, char *sn, int size);

int rmsl_init_device(int fd);
int rmsl_deinit_device(int fd);
int rmsl_reset_device(int fd);
int rmsl_suspend_device(int fd);

int rmsl_get_devices(char *dev_depth, char *dev_ir, char *dev_rgb, int silent);

struct rmsl_pc {
  float x;
  float y;
  float depth;
};

/*
 * Calculate the depth and point cloud from Depth sensor data.
 *
 * @pIn:        The Depth sensor data frame, in YUYV format.
 * @pc_out:     The output point cloud in #{struct rmsl_pc},
 *              array size should be at least @width * @height.
 * @depth_out:  The output depth whose array size at least @width * @height.
 * @width:      The width of frame.
 * @height:     The height of frame.
 *
 * Return 0 if depth or point cloud can be calculate from input data.
 * Return < 0 if something wrong.
 * Return > 0 for some RMSL modules that output depth values directly, not need
 * to convert.
 */
int rmsl_get_point_cloud_depth(const uint16_t *pIn, struct rmsl_pc *pc_out,
                                uint16_t *depth_out, int width, int height);
#ifdef __cplusplus
}
#endif

#endif
