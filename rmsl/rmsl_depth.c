#include <errno.h>
#include <stdio.h>

#include <rmsl_api.h>

#pragma pack(1)
typedef struct depth_ext_data_s
{
    uint16_t timestamp_s;
    uint32_t frameID;
    char reserved[2];
    int64_t timestamp;

    uint16_t tag;
    uint16_t size;
    uint16_t output_mode;
    uint16_t depth_multi_ratio;

    float fs;   // Focal length * Baseline / pixelSize
    float distance; // Reference Distance
} *depth_ext_data_t;
#pragma pack()

#define FOCAL_LENGTH        2.54
#define BASELINE            50
#define PIXELSIZE           0.003
#define REF_FRAME_DISTANCE  600

static inline float
getDistance(uint8_t high8, uint8_t low4, float fs, float d)
{
    if (high8 <= 0 && low4 <= 0)
        return 0;
    return (fs * d) / (fs + (((high8 - 64) + (double)low4 / 16)) * d);
}

static inline void
ConvertXYZ(int x, int y, float Depthz, float *fx, float *fy, float *fz, int iWidth, int iHeight)
{
    *fx = ((iWidth>>1) - x) * 0.0011811023622 * Depthz * 2;
    *fy = ((iHeight>>1) - y) * 0.0011811023622 * Depthz * 2;
    *fz = Depthz;
}

int rmsl_get_point_cloud_depth(const uint16_t *pIn, struct rmsl_pc *pc_out,
                               uint16_t *depth_out, int width, int height)
{
    uint8_t high8 = 0, low4 = 0;
    uint16_t depthOutputMode;
    float fx = 0, fy = 0, fz = 0, dis = 0;
    float fs = (FOCAL_LENGTH * BASELINE) / PIXELSIZE;
    float d = REF_FRAME_DISTANCE;

    depth_ext_data_t ext_tmp = (depth_ext_data_t)pIn;

    if (ext_tmp->tag == 0x5A5A) {
        depthOutputMode = ext_tmp->depth_multi_ratio;
        fs = ext_tmp->fs;
        d = ext_tmp->distance;
        if (0 == (int)fs || 0 == (int)d) {
            fprintf(stderr, "Invalid focal length(%f) or distance(%f)\n",
                    fs, d);
            return -EINVAL;
        }
    } else {
        depthOutputMode = -1;
    }

    if (depthOutputMode <= 0) {
        int x, y;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                high8 = (*pIn & 0xff0) >> 4;
                low4  = (*pIn & 0x00f);
                dis = getDistance(high8, low4, fs, d);

                if (NULL != pc_out) {
                    ConvertXYZ(x, y, dis, &fx, &fy, &fz, width, height);
                    pc_out[y * width + x].x = fx;
                    pc_out[y * width + x].y = fy;
                    pc_out[y * width + x].depth = fz;
                }
                if (NULL != depth_out)
                    *depth_out++ = (uint16_t)(dis + 0.5);

                pIn++;
            }
        }

        return 0;
    } else {
        /* The pIn already is depth values */
        return depthOutputMode;
    }
}
