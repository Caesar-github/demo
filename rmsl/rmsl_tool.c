#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include "rmsl_api.h"

static char dev_name[256] = { 0 };
static int cmd;

enum {
    CMD_INIT        = 1 << 0,
    CMD_DEINIT      = 1 << 1,
    CMD_RESET       = 1 << 2,
    CMD_GET_SN      = 1 << 3,
    CMD_GET_VERSION = 1 << 4,
    CMD_LIST_DEVICES = 1 << 5,
};

void parse_args(int argc, char **argv)
{
    int c;

    cmd = 0;

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"device",      required_argument, 0, 'd' },
            {"init",        no_argument,       0, 'i' },
            {"deinit",      no_argument,       0, 'e' },
            {"reset",       no_argument,       0, 'r' },
            {"get_sn",      no_argument,       0, 's' },
            {"get_version", no_argument,       0, 'v' },
            {"list_devices", no_argument,      0, 'l' },
            {0,             0,                 0,  0  }
        };

        c = getopt_long(argc, argv, "d:iersv",
            long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'd':
            strcpy(dev_name, optarg);
            break;
        case 'i':
            cmd |= CMD_INIT;
            break;
        case 'e':
            cmd |= CMD_DEINIT;
            break;
        case 'r':
            cmd |= CMD_RESET;
            break;
        case 's':
            cmd |= CMD_GET_SN;
            break;
        case 'v':
            cmd |= CMD_GET_VERSION;
            break;
        case 'l':
            cmd |= CMD_LIST_DEVICES;
            break;
        case '?':
            printf("Usage: %s to setup or query RMSL module.\n"
                   "    --device,       required, path of video device\n"
                   "    --get_sn,       optional, get the SN of module\n"
                   "    --get_version,  optional, get the Version of module\n"
                   "    --list_devices, optional, list the devices of RMSL module\n"
                   "    --init,         optional, init device to output data frame\n"
                   "    --deinit,       optional, deinit device\n"
                   "    --reset,        optional, reset device\n",
                   argv[0]);
            exit(-1);
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (strlen(dev_name) == 0 && (cmd & CMD_LIST_DEVICES) == 0) {
         printf("argument --device is required, use `%s --list_devices` to list devices.\n", argv[0]);
         exit(-1);
    }
}

void list_vdev()
{
    int i, ret, fd;
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

            printf("Device %s info:\n"
                   "      usb interface: %s"
                   "        driver name: %s\n"
                   "          card type: %s\n"
                   "           bus_info: %s\n",
                   vdev, usb_interface, cap.driver, cap.card, cap.bus_info);

        }

        close(fd);
    }
}

int main(int argc, char *argv[])
{
    int fd, ret;

    char str[RMSL_DATA_SIZE_QUERY];

    parse_args(argc, argv);

    fd = open(dev_name, O_RDWR | O_CLOEXEC, 0);

    if (cmd & CMD_GET_SN) {
        rmsl_get_sn(fd, str, RMSL_DATA_SIZE_QUERY);
        printf("SN: %s\n", str);
    }
    if (cmd & CMD_GET_VERSION) {
        rmsl_get_version(fd, str, RMSL_DATA_SIZE_QUERY);
        printf("Version: %s\n", str);
    }

    if (cmd & CMD_INIT) {
        if ((ret = rmsl_init_device(fd)) != 0)
            fprintf(stderr, "Init device failed, ret = %d\n", ret);
    }

    if (cmd & CMD_DEINIT) {
        if ((ret = rmsl_deinit_device(fd)) != 0)
            fprintf(stderr, "Deinit device failed, ret = %d\n", ret);
    }

    if (cmd & CMD_RESET) {
        if ((ret = rmsl_reset_device(fd)) != 0)
            fprintf(stderr, "Reset device failed, ret = %d\n", ret);
    }

    if (cmd & CMD_LIST_DEVICES) {
        list_vdev();
    }
    close(fd);

    return 0;
}
