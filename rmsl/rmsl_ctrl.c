#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>

#include "rmsl_api.h"

#define ERR(str, ...) do { \
        fprintf(stderr, "[ERR]%s:%d: " str, __func__, __LINE__, __VA_ARGS__); \
    } while (0)

#define RMSL_UNIT      0x06
#define RMSL_SELECTOR_CMD      0x01
#define RMSL_SELECTOR_QUERY    0x04

#define VDEV_DEPTH_TAG  "DEPTH"
#define VDEV_RGB_TAG    "RGB"
#define VDEV_IR_TAG     "IR"

int rmsl_get_devices(char *dev_depth, char *dev_rgb, char *dev_ir, int silent)
{
    int i, ret, fd, found = 0;
    char vdev[64] = {'0'};
    struct v4l2_capability cap;

    for (i = 0; ; i++) {
        sprintf(vdev, "/dev/video%d", i);

        fd = open(vdev, O_RDWR | O_CLOEXEC, 0);

        if (-1 == fd) {
            ret = errno;
            close(fd);
            if (ret == ENOENT || ret == ENOMEM)
                break;
            else
                continue;
        }

        ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
        if (-1 == ret) {
            close(fd);
            continue;
        }

        if (!strcmp("RV1108", (char *)cap.card) &&
            !(cap.device_caps & V4L2_CAP_META_CAPTURE)) {
            int usb_fd;
            char usb_name[256], usb_interface[256];

            memset(usb_name, 0, sizeof(usb_name));
            memset(usb_interface, 0, sizeof(usb_interface));

            sprintf(usb_name, "/sys/class/video4linux/video%d/device/interface", i);
            usb_fd = open(usb_name, O_RDONLY | O_CLOEXEC, 0);
            if (usb_fd < 0)
                sprintf(usb_interface, "Can not open %s\n", usb_name);

            read(usb_fd, usb_interface, 64);

            close(usb_fd);

            if (strstr(usb_interface, VDEV_DEPTH_TAG) && NULL != dev_depth)
                strcpy(dev_depth, vdev);
            else if (strstr(usb_interface, VDEV_RGB_TAG) && NULL != dev_rgb)
                strcpy(dev_rgb, vdev);
            else if (strstr(usb_interface, VDEV_IR_TAG) && NULL != dev_ir)
                strcpy(dev_ir, vdev);

            found++;

            if (!silent)
                printf("Device %s info:\n"
                   "      usb interface: %s"
                   "        driver name: %s\n"
                   "          card type: %s\n"
                   "           bus_info: %s\n",
                   vdev, usb_interface, cap.driver, cap.card, cap.bus_info);

        }

        close(fd);
    }

    return !(found == 3);
}

enum {
    RMSL_CMD_REBOOT_LOADER     = 0xFFFFFFFF, // reboot loader
    RMSL_CMD_REBOOT            = 0xFFFFFFFE, // reboot
    RMSL_CMD_SW_MASS_STORAGE   = 0xFFFFFFFC, // switch to MassStorage
    RMSL_CMD_DOWNLOAD          = 0xFFFFFFFB, // download calibration data
    RMSL_CMD_MODE_SCAN         = 0xFFFFFFFA, // scan code mode
    RMSL_CMD_MODE_FACE         = 0xFFFFFFF9, // scan face mode
    RMSL_CMD_MODE_FIX_FOCUS    = 0xFFFFFFF8, // fix focus mode
    RMSL_CMD_MODE_NORMAL       = 0xFFFFFFF7, // normal mode
    RMSL_CMD_FLIP              = 0xFFFFFFF6, // flip picture, deprecatd
    RMSL_CMD_RGB_TO_GRAY       = 0xFFFFFFF5, // RGB to grey
    RMSL_CMD_GREY_TO_RGB       = 0xFFFFFFF4, // grey to RGB
    RMSL_CMD_OPEN_DEVICE       = 0xFFFFFFF3, // open device
    RMSL_CMD_CLOSE_DEVICE      = 0xFFFFFFF2, // close device
    RMSL_CMD_CLOSE_RESET       = 0xFFFFFFF1, // close and reset device
    RMSL_CMD_CLOSE_RESET_SUSPEND = 0xFFFFFFF0, // close, reset and suspend
    RMSL_CMD_KEEP_STREAMING    = 0xFFFFFFEF, // keep streaming
    RMSL_CMD_ONLY_DEPTH        = 0xFFFFFFEE, // only output Depth
    RMSL_CMD_ONLY_IR           = 0xFFFFFFED, // only output IR
    RMSL_CMD_DEPTH_AND_IR      = 0xFFFFFFEC, // both Depth and IR
    RMSL_CMD_ENABLE_ADB        = 0xFFFFFFEB,
    RMSL_CMD_DISABLE_ISP_CROP  = 0xFFFFFFEA, // disable isp crop
    RMSL_CMD_ENABLE_ISP_CROP   = 0xFFFFFFE9, // enable isp crop
};

enum {
    RMSL_GET_VERSION       = 0x00,
    RMSL_GET_SN,
    RMSL_GET_DEPTH_RANGE,
    RMSL_GET_PCBA_SN,
    RMSL_GET_PCBA_RESULT,
    RMSL_GET_TEST_RESULT,
    RMSL_GET_MODEL,
    RMSL_SET_MIRROR,
    RMSL_GET_FUN_LIST,
};

static inline void rmsl_fill_query_data(char *data, int code)
{
    int *cmd = (int *) data;

    *cmd = code;
}

static int rmsl_query_set(int fd, int cmd)
{
    int ret;
    struct uvc_xu_control_query query = {
        .unit = RMSL_UNIT,
        .selector = RMSL_SELECTOR_CMD,
        .query = UVC_SET_CUR,
        .size = RMSL_DATA_SIZE_CMD,
        .data = (unsigned char *)&cmd,
    };

    ret = ioctl(fd, UVCIOC_CTRL_QUERY, &query);
    if (ret) {
        ERR("ioctl error: %d: %s\n", errno, strerror(errno));
        return ret;
    }

    return 0;
}

static int rmsl_query_get(int fd, char *data, int size, int code)
{
    int ret;
    struct uvc_xu_control_query query = {
        .unit = RMSL_UNIT,
        .selector = RMSL_SELECTOR_QUERY,
        .query = UVC_SET_CUR,
        .size = RMSL_DATA_SIZE_QUERY,
        .data = (unsigned char *)data,
    };

    if (size < RMSL_DATA_SIZE_QUERY) {
        ERR("Buffer size %d < required %d\n", size, RMSL_DATA_SIZE_QUERY);
        return -EINVAL;
    }

    rmsl_fill_query_data(data, code);
    ret = ioctl(fd, UVCIOC_CTRL_QUERY, &query);
    if (ret) {
        ERR("ioctl error: %d: %s\n", errno, strerror(errno));
        return ret;
    }

    query.query = UVC_GET_CUR;
    ret = ioctl(fd, UVCIOC_CTRL_QUERY, &query);
    if (ret) {
        ERR("ioctl error: %d: %s\n", errno, strerror(errno));
        return ret;
    }

    return 0;
}

int rmsl_get_version(int fd, char *ver, int size)
{
    return rmsl_query_get(fd, ver, size, RMSL_GET_VERSION);
}

int rmsl_get_sn(int fd, char *sn, int size)
{
    return rmsl_query_get(fd, sn, size, RMSL_GET_SN);
}

int rmsl_reboot_to_loader(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_REBOOT_LOADER);
}

int rmsl_reboot(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_REBOOT);
}

int rmsl_switch_to_mass_storage(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_SW_MASS_STORAGE);
}

int rmsl_set_mode_scan_code(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_MODE_SCAN);
}

int rmsl_set_mode_scan_face(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_MODE_FACE);
}

int rmsl_set_mode_fix_foucs(int fd, unsigned char focus)
{
    return rmsl_query_set(fd, RMSL_CMD_MODE_FIX_FOCUS | (focus << 24));
}

int rmsl_set_mode_normal(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_MODE_NORMAL);
}

int rmsl_set_output_grey(int fd, int enable)
{
    return rmsl_query_set(fd, enable ? RMSL_CMD_RGB_TO_GRAY : RMSL_CMD_GREY_TO_RGB);
}

int rmsl_init_device(int fd)
{
    int ret;

    ret = rmsl_query_set(fd, RMSL_CMD_OPEN_DEVICE);
    ret |= rmsl_query_set(fd, RMSL_CMD_KEEP_STREAMING);

    return ret;
}

int rmsl_deinit_device(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_CLOSE_DEVICE);
}

int rmsl_reset_device(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_CLOSE_RESET);
}

int rmsl_suspend_device(int fd)
{
    return rmsl_query_set(fd, RMSL_CMD_CLOSE_RESET_SUSPEND);
}
