#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#include <rga/RgaApi.h>

#include "camera_engine_rkisp/interface/rkisp_api.h"
#include "rmsl_api.h"
#include "rkdrm_display.h"
#include "vpu_decode.h"

#define BUF_COUNT       4

#define FILE_PATH_LEN   256
#define FRAME_WIDTH     640
#define FRAME_HEIGHT    480

#define print_once(fmt, ...)        \
({                                  \
    static int __print_once;        \
                                    \
    if (!__print_once) {            \
        __print_once = 1;           \
        printf(fmt, ##__VA_ARGS__); \
    }                               \
})

/* Copy from kernel source */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define MIN(x, y) ((x) < (y) ? (x) : (y))

struct vdev_ctx {
    const char   *dev_desc;
    char         dev_name[FILE_PATH_LEN];

    const struct rkisp_api_ctx  *api;
    const struct rkisp_api_buf  *cur_frame;

    /* The sensor data frame info */
    int width;
    int height;
    int fcc;

    /* The buffer from RGA for mpp decoder output */
    bo_t rga_bo;
    int rga_bo_fd;
    struct vpu_decode decoder;

    /* The src & dst info, basing on them RGA copy src to dst display buffer */
    rga_info_t rga_src;
    rga_info_t rga_dst;
    struct {
        int x;
        int y;
        int w;
        int h;
    } display_rect;

    /* Save decoded frames to file if not NULL */
    FILE *save_fp;
};

static struct vdev_ctx g_ctx_depth = {
    .dev_desc   = "UVC DEPTH",
    .width      = FRAME_WIDTH,
    .height     = FRAME_HEIGHT,
    .fcc        = V4L2_PIX_FMT_YUYV,
    .save_fp    = NULL,
};

static struct vdev_ctx g_ctx_rgb = {
    .dev_desc   = "UVC RGB",
    .width      = FRAME_WIDTH,
    .height     = FRAME_HEIGHT,
    .fcc        = V4L2_PIX_FMT_MJPEG,
    .save_fp    = NULL,
};

static struct vdev_ctx g_ctx_ir = {
    .dev_desc   = "UVC IR",
    .width      = FRAME_WIDTH,
    .height     = FRAME_HEIGHT,
    .fcc        = V4L2_PIX_FMT_MJPEG,
    .save_fp    = NULL,
};


#define DISP_BUF_COUNT      3
struct disp_ctx {
    struct vdev_ctx *video_devs[3];

    struct drm_dev dev;
    int width;
    int height;
    int format;
    int size;
    int x_offset;
    int y_offset;
    int plane_type;
    struct drm_buf drm_bufs[DISP_BUF_COUNT];
    int buf_active;

    /* function control */
    int en_display;
    int screen_width;
    int screen_height;
};

extern int c_RkRgaFree(bo_t *bo_info);

int rga_buffer_alloc(bo_t *bo, int *buf_fd, int width, int height, int bpp)
{
    int ret;

    ret = c_RkRgaGetAllocBuffer(bo, width, height, bpp);
    if (ret) {
        printf("c_RkRgaGetAllocBuffer error : %s\n", strerror(errno));
        return ret;
    }

    ret = c_RkRgaGetMmap(bo);
    if (ret) {
        printf("c_RkRgaGetMmap error : %s\n", strerror(errno));
        return ret;
    }

    ret = c_RkRgaGetBufferFd(bo, buf_fd);
    if (ret) {
        printf("c_RkRgaGetBufferFd error : %s\n", strerror(errno));
        return ret;
    }

    return 0;
}

void rga_buffer_free(bo_t *bo, int buf_fd)
{
    int ret;

    if (buf_fd >= 0)
        close(buf_fd);
    ret = c_RkRgaUnmap(bo);
    if (ret)
        printf("c_RkRgaUnmap error : %s\n", strerror(errno));
    ret = c_RkRgaFree(bo);
    if (ret)
        printf("c_RkRgaFree error : %s\n", strerror(errno));
}

/*
 * Fill three videos in two rows. So the max display size is [1280x960].
 */
void fill_disp_info(struct disp_ctx *disp)
{
    struct vdev_ctx *ctx_depth, *ctx_rgb, *ctx_ir;
    struct vdev_ctx *ctx;
    int width, height, pad_align = 8;
    int x_offset, i;

    ctx_depth = disp->video_devs[0];
    ctx_rgb   = disp->video_devs[1];
    ctx_ir    = disp->video_devs[2];

    width = FRAME_WIDTH;
    height = FRAME_HEIGHT;

    /* Vop require 32 width align */
    disp->width = MIN(round_down(disp->screen_width, 32), width * 2);
    disp->height = MIN(disp->screen_height, height * 2);
    disp->x_offset = (disp->screen_width - disp->width) / 2;
    disp->y_offset = (disp->screen_height - disp->height) / 2;

    width = disp->width / 2;
    height = disp->height / 2;

    x_offset = round_down((disp->width - width) / 2, pad_align);
    ctx_rgb->display_rect.x = x_offset;
    ctx_rgb->display_rect.y = 0;
    ctx_rgb->display_rect.w = width;
    ctx_rgb->display_rect.h = height;

    ctx_depth->display_rect.x = 0;
    ctx_depth->display_rect.y = height;
    ctx_depth->display_rect.w = width;
    ctx_depth->display_rect.h = height;

    ctx_ir->display_rect.x = width;
    ctx_ir->display_rect.y = height;
    ctx_ir->display_rect.w = width;
    ctx_ir->display_rect.h = height;

    for (i = 0; i < 3; i++) {
        int fmt;

        ctx = disp->video_devs[i];
        printf("%s: %s: draw in (%d, %d)[%d x %d]\n",
               ctx->dev_desc, ctx->dev_name,
               ctx->display_rect.x,
               ctx->display_rect.y,
               ctx->display_rect.w,
               ctx->display_rect.h);

        fmt = V4L2_PIX_FMT_MJPEG == ctx->fcc ? RK_FORMAT_YCrCb_420_SP :
                                               RK_FORMAT_RGB_565;
        /* Set the src & dst rects for RGA Copy. */
        rga_set_rect(&ctx->rga_src.rect, 0, 0, ctx->width, ctx->height,
                     ctx->width, ctx->height, /* stride width & height */
                     fmt);
        rga_set_rect(&ctx->rga_dst.rect,
                     ctx->display_rect.x, ctx->display_rect.y,
                     ctx->display_rect.w, ctx->display_rect.h,
                     disp->width, disp->height, /* stride width & height */
                     RK_FORMAT_RGB_565);
    }
}

int wait_frames(struct disp_ctx *disp, fd_set *fds)
{
    int max_fd = 0, timeout_ms = 3000, i, ret;
    struct vdev_ctx *ctx;
    struct timeval tv;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(fds);
    for (i = 0; i < 3; i++) {
        ctx = disp->video_devs[i];
        FD_SET(ctx->api->fd, fds);
        max_fd = ctx->api->fd > max_fd ? ctx->api->fd : max_fd;
    }

    while (timeout_ms > 0) {
        ret = select(max_fd + 1, fds, NULL, NULL, &tv);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                fprintf(stderr, "select() return error: %s\n", strerror(errno));
                return ret;
            }
        } else if (ret) {
            /* Data ready, FD_ISSET(0, &fds) will be true. */
            break;
        } else {
            /* timeout */
            fprintf(stderr, "Wait for data frames timeout\n");
        }
    }

    return ret;
}

int rga_copy_for_display(struct disp_ctx *disp, struct vdev_ctx *ctx)
{
    int ret;

    if (V4L2_PIX_FMT_MJPEG == ctx->fcc) {
        ctx->rga_src.fd = ctx->rga_bo_fd;
    } else {
        ctx->rga_src.fd = ctx->cur_frame->fd;
    }
    ctx->rga_dst.fd = disp->drm_bufs[disp->buf_active].dmabuf_fd;
    ctx->rga_src.mmuFlag = 1;
    ctx->rga_dst.mmuFlag = 1;
    ret = c_RkRgaBlit(&ctx->rga_src, &ctx->rga_dst, NULL);
    if (ret) {
        fprintf(stderr, "RGA Copy buffer failed, ret = %d\n", ret);
        return ret;
    }

    return 0;
}

int write_to_file(FILE *fp, void *buf, int size)
{
    int count, wrote;

    count = size / 1024;
    wrote = 0;
    do {
        wrote = fwrite(buf + wrote * 1024, 1024, count, fp);
    } while (wrote != count && !ferror(fp));

    return ferror(fp);
}

void loop(struct disp_ctx *disp)
{
    int i, ret, ready;
    struct vdev_ctx *ctx;
    struct timeval t_start, t_stop;
    fd_set fds;

    if (disp->en_display)
        disp->buf_active = 0;

    while (1) {
        if (wait_frames(disp, &fds) <= 0) {
            /* Timeout or error */
            return;
        }

        gettimeofday(&t_start, NULL);

        ready = 1;
        for (i = 0; i < 3; i++) {
            ctx = disp->video_devs[i];

            if (!FD_ISSET(ctx->api->fd, &fds)) {
                if (!ctx->cur_frame)
                    ready = 0;
                continue;
            }

            /* a new frame is pending */

            if (ctx->cur_frame)
                rkisp_put_frame(ctx->api, ctx->cur_frame);

            ctx->cur_frame = rkisp_get_frame(ctx->api, 0);
            if (NULL == ctx->cur_frame) {
                fprintf(stderr, "Can not read frame from %s\n", ctx->dev_desc);
                return;
            }

            if (V4L2_PIX_FMT_MJPEG == ctx->fcc) {
                vpu_decode_jpeg_doing(&ctx->decoder, ctx->cur_frame->buf, ctx->cur_frame->size,
                                      ctx->rga_bo_fd, ctx->rga_bo.ptr);
                if (ctx->save_fp)
                    write_to_file(ctx->save_fp, ctx->rga_bo.ptr,
                                  ctx->api->width * ctx->api->height * 3 / 2);
            } else if (V4L2_PIX_FMT_YUYV == ctx->fcc && ctx->save_fp) {
                /* The coverting to depth is cpu-busy */
                ret = rmsl_get_point_cloud_depth(ctx->cur_frame->buf, NULL,
                                                 ctx->rga_bo.ptr,
                                                 ctx->width, ctx->height);
                if (ret != 0) {
                    /* The buf->buf already is depth values or data corrupted. */
                    fprintf(stderr, "Depth data frame corrupted? Skip it.\n");
                    rkisp_put_frame(ctx->api, ctx->cur_frame);
                    ctx->cur_frame = NULL;
                    ready = 0;
                    continue;
                } else {
                    write_to_file(ctx->save_fp, ctx->rga_bo.ptr,
                                  ctx->api->width * ctx->api->height * 2);
                }
            }

            if (disp->en_display)
                rga_copy_for_display(disp, ctx);
        }

        if (!ready)
            continue;

        if (disp->en_display) {
            ret = drmCommit(&disp->drm_bufs[disp->buf_active],
                            disp->width, disp->height,
                            disp->x_offset, disp->y_offset,
                            &disp->dev, disp->plane_type);
            if (ret) {
                fprintf(stderr, "display commit error, ret = %d\n", ret);
            }
            disp->buf_active = (disp->buf_active + 1) % 3;
        } else {
            gettimeofday(&t_stop, NULL);
            printf("> %ldms\n", 1000 * (t_stop.tv_sec - t_start.tv_sec) +
                               (t_stop.tv_usec - t_start.tv_usec) / 1000);
        }

        for (i = 0; i < 3; i++) {
            ctx = disp->video_devs[i];
            rkisp_put_frame(ctx->api, ctx->cur_frame);
            ctx->cur_frame = NULL;
        }
    }
}

int parse_args(struct disp_ctx *disp, int argc, char **argv)
{
    int c;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"screen-width",    required_argument, 0, 'w' },
            {"screen-height",   required_argument, 0, 'h' },
            {"no-display",      no_argument,       0, 'n' },
            {"save-depth-to",   required_argument, 0, 'd' },
            {"save-ir-to",      required_argument, 0, 'i' },
            {"save-rgb-to",     required_argument, 0, 'r' },
            {"help",            no_argument,       0, 'p' },
            {0,                 0,                 0,  0  }
        };

        c = getopt_long(argc, argv, "w:h:i:d:r:np", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'w':
            disp->screen_width = atoi(optarg);
            break;
        case 'h':
            disp->screen_height = atoi(optarg);
            break;
        case 'n':
            disp->en_display = 0;
            break;
        case 'i':
            if ((g_ctx_ir.save_fp = fopen(optarg, "we")) == NULL) {
                fprintf(stderr, "Open %s failed\n", optarg);
                return -1;
            }
            break;
        case 'd':
            if ((g_ctx_depth.save_fp = fopen(optarg, "we")) == NULL) {
                fprintf(stderr, "Open %s failed\n", optarg);
                return -1;
            }
            break;
        case 'r':
            if ((g_ctx_rgb.save_fp = fopen(optarg, "we")) == NULL) {
                fprintf(stderr, "Open %s failed\n", optarg);
                return -1;
            }
            break;
        case '?':
        case 'p':
            fprintf(stderr, "Usage of %s:\n"
                "To display and/or save decode frames to files:\n"
                " --screen-width,   screen width,  required if need display\n"
                " --screen-height,  screen height, required if need display\n"
                " --no-display,     disable display, by default it's enabled\n"
                " --save-ir-to,     path to save IR    decoded frames in NV12\n"
                " --save-depth-to,  path to save DEPTH decoded frames with bpp = 16\n"
                " --save-rgb-to,    path to save RGB   decoded frames in NV12\n",
                argv[0]);
            return -1;
        default:
            fprintf(stderr, "?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (disp->en_display && (!disp->screen_width || !disp->screen_height)) {
         fprintf(stderr, "arguments --screen-width and --screen-height are required when display enable\n");
         return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    struct disp_ctx disp = {
        .format         = DRM_FORMAT_BGR565,
        .x_offset       = 0,
        .y_offset       = 0,
        .plane_type     = DRM_PLANE_TYPE_OVERLAY,
        .video_devs     = {&g_ctx_depth, &g_ctx_rgb, &g_ctx_ir},
        .en_display     = 1,
        .screen_width   = 0,
        .screen_height  = 0,
    };
    int ret, i;

    if (parse_args(&disp, argc, argv))
        return -1;

    ret = rmsl_get_devices(g_ctx_depth.dev_name, g_ctx_rgb.dev_name,
                           g_ctx_ir.dev_name, 1);
    if (ret) {
        fprintf(stderr, "Can not get 3 usb video devices,"
                        " please check `rmsl_tool --list_devices`\n");
        return -EINVAL;
    }

    if (disp.en_display) { /* Init display devices and alloc buffers */
        fill_disp_info(&disp);

        if (drmInit(&disp.dev)) {
            fprintf(stderr, "drmInit: Failed\n");
            return -1;
        }

        for (i = 0; i < DISP_BUF_COUNT; i++) {
            ret = drmGetBuffer(disp.dev.drm_fd, disp.width, disp.height,
                               disp.format, &disp.drm_bufs[i]);
            if (ret) {
                fprintf(stderr, "Alloc drm buffer failed, %d\n", i);
                return -1;
            }
            memset(disp.drm_bufs[i].map, 0xff, disp.drm_bufs[i].size);
        }
    }

    { /* Open rga device */
        ret = c_RkRgaInit();
        if (ret) {
            fprintf(stderr, "c_RkRgaInit error : %s\n", strerror(errno));
            return ret;
        }
    }

    for (i = 0; i < 3; i++) {
        struct vdev_ctx *ctx = disp.video_devs[i];

        if (V4L2_PIX_FMT_MJPEG == ctx->fcc) {
            ret = vpu_decode_jpeg_init(&ctx->decoder, ctx->width, ctx->height);
            if (ret) {
                fprintf(stderr, "init jpeg decoder failed, %d\n", ret);
                return ret;
            }
        }
        rga_buffer_alloc(&ctx->rga_bo, &ctx->rga_bo_fd, ctx->width, ctx->height, 16);
        if (ret) {
            fprintf(stderr, "init rga buffer failed, %d\n", ret);
            return ret;
        }

        ctx->api = rkisp_open_device(ctx->dev_name, 0);
        if (ctx->api == NULL)
            return -1;

        rmsl_init_device(ctx->api->fd);
        rkisp_set_fmt(ctx->api, ctx->width, ctx->height, ctx->fcc);
    }

    /* Start capture after all devices are setup and to DQ buffers ASAP */
    for (i = 0; i < 3; i++) {
        struct vdev_ctx *ctx = disp.video_devs[i];
        if (rkisp_start_capture(ctx->api))
            return -1;
    }

    loop(&disp);

    for (i = 0; i < 3; i++) {
        struct vdev_ctx *ctx = disp.video_devs[i];

        rkisp_stop_capture(ctx->api);
        rmsl_deinit_device(ctx->api->fd);
        rkisp_close_device(ctx->api);
        if (V4L2_PIX_FMT_MJPEG == ctx->fcc) {
            vpu_decode_jpeg_done(&ctx->decoder);
        }
        rga_buffer_free(&ctx->rga_bo, ctx->rga_bo_fd);

        if (ctx->save_fp)
            fclose(ctx->save_fp);
    }

    if (disp.en_display) {
        drmDeinit(&disp.dev);
        for (i = 0; i < DISP_BUF_COUNT; i++)
            drmPutBuffer(disp.dev.drm_fd, &disp.drm_bufs[i]);
    }

    c_RkRgaDeInit();

    return 0;
}
