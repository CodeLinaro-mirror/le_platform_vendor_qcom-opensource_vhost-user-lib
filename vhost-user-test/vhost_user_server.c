/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <stdlib.h>
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <vhost_user.h>


void test_set_features(struct vhost_user_dev *dev, uint64_t features) 
{
    printf("features is %lx\n", features);
}

uint64_t test_get_features(struct vhost_user_dev *dev)
{
    return 0x100000000UL;
}

int test_set_vring_state(struct vhost_user_dev *dev, uint32_t idx, uint32_t state)
{
    printf("set virng [%d] state = %d\n", idx, state);
    return 0;
}
// this is device specific, so provide these handler in each device.
struct vhost_dev_ops dev_ops = {
    .set_features = test_set_features,
    .get_features = test_get_features,
    .set_vring_state = test_set_vring_state,
};

int run_vhost_user_server(char *socket_path)
{
    // Create a new device
    struct vhost_user_dev *vudev;
    int ret = 0;

    if (!socket_path) {
        printf("please provide socket path\n");
        return -1;
    }

    vudev = calloc(sizeof(struct vhost_user_dev), 1);
    if (!vudev) {
        printf("failed to alloc memory for vhost_user_dev\n");
        exit(-1);
    }
    if (vhost_user_init_device(vudev, &dev_ops) < 0) {
        ret = -1;
        goto err;
    }
    if (vhost_user_wait_for_connect(vudev, socket_path) < 0) {
        printf("failed to make connection with client\n");
        ret = -1;
        goto err;
    }
    vhost_user_start_loop(vudev);
    // socket is close

err:
    vhost_user_deinit_device(vudev);
    free(vudev);
    return ret;

}

