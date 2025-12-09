/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

/* Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <errno.h>
#include "vhost_user.h"
#include "vu_common.h"
#include "vu_socket.h"


static int log_info = 1;
static int log_debug;

#define pr_info(fmt, args...) do {if (log_info)  printf(fmt, ##args);} while(0)
#define pr_debug(fmt, args...) do {if (log_debug)  printf(fmt, ##args);} while(0)
#define pr_err(fmt, args...) do { printf(fmt, ##args);} while(0)

// Enable the feature VIRTIO_F_VERSION_1 by default for all the devices.
#define VHOST_USER_SUPPORT_FEAT  (1UL << VIRTIO_F_VERSION_1)

typedef struct vhost_message_handler {
    const char *description;
    int (*callback)(struct vhost_user_dev *dev, struct vhu_msg_context *ctx);
    bool accepts_fd;
} vhost_message_handler_t;

static void
init_vring_queue(struct vhost_user_dev *dev, struct vhost_virtqueue *vq, uint32_t vring_idx)
{
    vq->index = vring_idx;
    vq->vudev = dev;
    vq->kickfd = VIRTIO_INVALID_EVENTFD;
    vq->callfd = VIRTIO_INVALID_EVENTFD;
    atomic_init(&vq->started, false);
}

int
alloc_vring_queue(struct vhost_user_dev *dev, uint32_t vring_idx)
{
    struct vhost_virtqueue *vq;
    uint32_t i;
    int ret = 0;

    if (!dev) {
        pr_err("Invalid dev pointer in alloc_vring_queue\n");
        return -1;
    }

    /* Also allocate holes, if any, up to requested vring index. */
    for (i = 0; i <= vring_idx; i++) {
        if (dev->virtqueue[i])
            continue;

        vq = calloc(sizeof(struct vhost_virtqueue), 1);
        if (vq == NULL) {
            pr_err("failed to allocate memory for vring %u.\n", i);
            ret = -1;
            break;
        }
        init_vring_queue(dev, vq, i);
        dev->virtqueue[i] = vq;
    }

    if (ret < 0) {
        // Cleanup on failure
        for (i = 0; i <= vring_idx; i++) {
            if (dev->virtqueue[i]) {
                free(dev->virtqueue[i]);
                dev->virtqueue[i] = NULL;
            }
        }
        return ret;
    }

    dev->nr_vring = MAX(dev->nr_vring, vring_idx + 1);

    return 0;
}

static int
vhost_user_check_and_alloc_queue_pair(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint32_t vring_idx;

    if (!dev || !ctx) {
        pr_err("Invalid dev or ctx pointer in vhost_user_check_and_alloc_queue_pair\n");
        return -1;
    }

    switch (ctx->msg.request.frontend) {
    case VHOST_USER_SET_VRING_KICK:
    case VHOST_USER_SET_VRING_CALL:
    case VHOST_USER_SET_VRING_ERR:
        vring_idx = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
        break;
    case VHOST_USER_SET_VRING_NUM:
    case VHOST_USER_SET_VRING_BASE:
    case VHOST_USER_GET_VRING_BASE:
    case VHOST_USER_SET_VRING_ENABLE:
        vring_idx = ctx->msg.payload.state.index;
        break;
    case VHOST_USER_SET_VRING_ADDR:
        vring_idx = ctx->msg.payload.addr.index;
        break;
    default:
        return 0;
    }

    if (vring_idx >= VHOST_MAX_VRING) {
        pr_err("invalid vring index: %u\n", vring_idx);
        return -1;
    }

    if (dev->virtqueue[vring_idx])
        return 0;

    return alloc_vring_queue(dev, vring_idx);
}

/*
 * The features that we support are requested.
 */
static int
vhost_user_get_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = 0;

    if (dev->dev_ops->get_features)
        features = dev->dev_ops->get_features(dev);

    ctx->msg.payload.u64 = features | VHOST_USER_SUPPORT_FEAT;
    ctx->msg.size = sizeof(ctx->msg.payload.u64);
    ctx->fd_num = 0;
    pr_debug("get features %lx \n", ctx->msg.payload.u64);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = ctx->msg.payload.u64;

    if (dev->dev_ops->set_features)
        dev->dev_ops->set_features(dev, features);

    dev->negotiated_feats = features;
    pr_debug("set features: 0x%lx\n", features);

    return VHOST_MSG_RESULT_OK;
}

/*
 * The config that we support are requested.
 */
static int
vhost_user_get_config(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret;

    uint32_t offset = ctx->msg.payload.config.offset;
    uint32_t size = ctx->msg.payload.config.size;
    char *payload = &(ctx->msg.payload.config.payload);

    if (dev->dev_ops->get_config) {
        ret = dev->dev_ops->get_config(dev, offset, size, payload);

        if (ret < 0) {
            pr_err("failed to get config! offset: 0x%x size: 0x%x \n", offset, size);
            /* vhost-user back-end uses zero length of payload to indicate
            an error to the vhost-user front-end.*/
            ctx->msg.payload.config.size = 0;
        }
    } else {
        pr_err("set config is not support!\n");
    }

    pr_debug("get config, offset: 0x%x size: 0x%x \n", offset, size);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_config(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    int ret;
    uint32_t offset = ctx->msg.payload.config.offset;
    uint32_t size = ctx->msg.payload.config.size;
    uint32_t flag = ctx->msg.payload.config.flag;
    char *payload = &(ctx->msg.payload.config.payload);

    if (dev->dev_ops->set_config) {
            ret = dev->dev_ops->set_config(dev, offset, size, flag, payload);
        if (ret < 0) {
            pr_err("failed to set config! offset: 0x%x size: 0x%x flag: 0x%x\n",
                offset, size, flag);
            return VHOST_MSG_RESULT_ERR;
        }
    } else {
        pr_err("set config is not support!\n");
    }

    pr_debug("set config, offset: 0x%x size: 0x%x flag: 0x%x\n", offset, size, flag);

    return VHOST_MSG_RESULT_OK;
}

/*
 * This function just returns success at the moment unless
 * the device hasn't been initialised.
 */
static int
vhost_user_set_owner(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_mmap_region(struct vhost_user_dev *dev,
        struct vhost_mem_region *region,
        uint64_t mmap_offset)
{
    void *mmap_addr;
    uint64_t mmap_size;

    /* Check for memory_size + mmap_offset overflow */
    if (mmap_offset >= -region->size) {
        pr_err("mmap_offset (0x%lx) and memory_size (0x%lx) overflow\n",
            mmap_offset, region->size);
        return -1;
    }

    mmap_size = region->size + mmap_offset;

    mmap_addr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
            MAP_SHARED, region->fd, 0);

    if (mmap_addr == MAP_FAILED) {
        pr_err("mmap failed (%s).\n", strerror(errno));
        return -1;
    }

    region->mmap_addr = mmap_addr;
    region->mmap_size = mmap_size;
    region->host_user_addr = (uint64_t)(uintptr_t)mmap_addr + mmap_offset;

    pr_debug("guest memory region size: 0x%lx\n", region->size);
    pr_debug("\t guest physical addr: 0x%lx\n", region->guest_phys_addr);
    pr_debug("\t guest virtual  addr: 0x%lx\n", region->guest_user_addr);
    pr_debug("\t host  virtual  addr: 0x%lx\n", region->host_user_addr);
    pr_debug("\t mmap addr : 0x%lx\n", (uint64_t)(uintptr_t)mmap_addr);
    pr_debug("\t mmap size : 0x%lx\n", mmap_size);
    pr_debug("\t mmap off  : 0x%lx\n", mmap_offset);

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_mem_table(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct VhostUserMemory *memory = &ctx->msg.payload.memory;
    struct vhost_mem_region *reg;
    uint64_t mmap_offset;
    uint32_t i;

    if (memory->nregions > VHOST_MEMORY_MAX_NREGIONS) {
        pr_err("too many memory regions (%u)\n",
            memory->nregions);
            return -1;
    }
    if (ctx->fd_num != memory->nregions) {
        pr_err("fd number is not consistent with nregions\n");
        return -1;
    }
    dev->mem = calloc(sizeof(struct vhost_memory) + sizeof(struct vhost_mem_region) * memory->nregions, 1);
    if (dev->mem == NULL) {
        pr_err("failed to allocate memory\n");
        return -1;
    }
    for (i = 0; i < memory->nregions; i++) {
        reg = &dev->mem->regions[i];

        reg->guest_phys_addr = memory->regions[i].guest_phys_addr;
        reg->guest_user_addr = memory->regions[i].userspace_addr;
        reg->size            = memory->regions[i].memory_size;
        reg->fd              = ctx->fds[i];

        /*
         * Assign invalid file descriptor value to avoid double
         * closing on error path.
         */

        mmap_offset = memory->regions[i].mmap_offset;

        if (vhost_user_mmap_region(dev, reg, mmap_offset) < 0) {
            pr_err("failed to mmap region %u\n", i);
            return -1;
        }

        dev->mem->nregions++;
    }
    return 0;

}

/*
 * The virtio device sends us the size of the descriptor ring.
 */
static int
vhost_user_set_vring_num(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_virtqueue *vq;

    if (!dev || !ctx) {
        pr_err("Invalid dev or ctx pointer in vhost_user_set_vring_num\n");
        return VHOST_MSG_RESULT_ERR;
    }

    if (ctx->msg.payload.state.index >= VHOST_MAX_VRING) {
        pr_err("Invalid vring index: %u\n", ctx->msg.payload.state.index);
        return VHOST_MSG_RESULT_ERR;
    }

    vq = dev->virtqueue[ctx->msg.payload.state.index];
    if (!vq) {
        pr_err("Invalid virtqueue pointer for index %u\n", ctx->msg.payload.state.index);
        return VHOST_MSG_RESULT_ERR;
    }

    if (ctx->msg.payload.state.num > 32768) {
        pr_err("invalid virtqueue size %u\n",
            ctx->msg.payload.state.num);
        return VHOST_MSG_RESULT_ERR;
    }

    vq->size = ctx->msg.payload.state.num;

    return VHOST_MSG_RESULT_OK;
}

void
vring_invalidate(struct vhost_user_dev *dev , struct vhost_virtqueue *vq)
{
    vq->desc = NULL;
    vq->avail = NULL;
    vq->used = NULL;

}

/* Converts VMM virtual address to Vhost virtual address. */
static uint64_t
qva_to_vva(struct vhost_user_dev *dev, uint64_t qva)
{
    struct vhost_mem_region *r;
    uint32_t i;

    if (!dev || !dev->mem)
        return 0;

    /* Find the region where the address lives. */
    for (i = 0; i < dev->mem->nregions; i++) {
        r = &dev->mem->regions[i];

        if (qva >= r->guest_user_addr &&
            qva <  r->guest_user_addr + r->size) {
            return qva - r->guest_user_addr +
                   r->host_user_addr;
        }
    }

    return 0;
}

static void
setup_ring_addr(struct vhost_user_dev *dev, struct vhost_virtqueue *vq)
{
    if (!dev || !vq) {
        pr_err("Invalid dev or vq pointer in setup_ring_addr\n");
        return;
    }

    /* The addresses are converted from VMM virtual to Vhost virtual. */
    if (vq->desc && vq->avail && vq->used)
        return;

    vq->desc = (struct virtq_desc *)qva_to_vva(dev, vq->ring_addrs.desc_user_addr);
    if (vq->desc == 0) {
        pr_err("failed to map desc ring.\n");
        return;
    }

    vq->avail = (struct virtq_avail *)qva_to_vva(dev,vq->ring_addrs.avail_user_addr);
    if (vq->avail == 0) {
        pr_err("failed to map avail ring.\n");
        return;
    }

    vq->used = (struct virtq_used *)qva_to_vva(dev,vq->ring_addrs.used_user_addr);
    if (vq->used == 0) {
        pr_err("failed to map used ring.\n");
        return;
    }

    if (vq->last_used_idx != vq->used->idx) {
        pr_err("last_used_idx (%u) and vq->used->idx (%u) mismatches;\n",
            vq->last_used_idx, vq->used->idx);
        vq->last_used_idx  = vq->used->idx;
        vq->last_avail_idx = vq->used->idx;
    }

    pr_debug("mapped address desc: %p\n", vq->desc);
    pr_debug("mapped address avail: %p\n", vq->avail);
    pr_debug("mapped address used: %p\n", vq->used);
}


/*
 * The virtio device sends us the desc, used and avail ring addresses.
 * This function then converts these to our address space.
 */
static int
vhost_user_set_vring_addr(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_virtqueue *vq;
    struct vhost_vring_addr *addr = &ctx->msg.payload.addr;

    if (dev->mem == NULL)
        return VHOST_MSG_RESULT_ERR;

    /* addr->index refers to the queue index.*/
    vq = dev->virtqueue[ctx->msg.payload.addr.index];

    /*
     * Rings addresses should not be interpreted as long as the ring is not
     * started and enabled
     */
    memcpy(&vq->ring_addrs, addr, sizeof(*addr));

    setup_ring_addr(dev, vq);

    return VHOST_MSG_RESULT_OK;
}

/*
 * The virtio device sends us the available ring last used index.
 */
static int
vhost_user_set_vring_base(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_virtqueue *vq = dev->virtqueue[ctx->msg.payload.state.index];

    vq->last_used_idx = ctx->msg.payload.state.num;
    vq->last_avail_idx = ctx->msg.payload.state.num;

    pr_debug("vring base idx:%u last_used_idx:%u last_avail_idx:%u.\n",
        ctx->msg.payload.state.index, vq->last_used_idx, vq->last_avail_idx);

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_state(struct vhost_user_dev *dev, struct vhost_virtqueue *vq,
    int enable)
{
    int ret = 0;

    if (dev->dev_ops->set_vring_state) {
        ret = dev->dev_ops->set_vring_state(dev, vq->index, enable);
    } else {
        pr_info("Note! set_vring_state is not provided by the vhost user device!\n");
    }

    return ret;
}

/*
 * when virtio is stopped, VMM will send us the GET_VRING_BASE message.
 */
static int
vhost_user_get_vring_base(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_virtqueue *vq = dev->virtqueue[ctx->msg.payload.state.index];

    /* We have to stop the queue (virtio) if it is running. */
    if (vhost_user_set_vring_state(dev, vq, 0) < 0) {
        pr_err("failed to disable the vring!! \n");
        return VHOST_MSG_RESULT_ERR;
    }

    ctx->msg.payload.state.num = vq->last_avail_idx;

    pr_debug("vring base idx:%d file:%d\n",
        ctx->msg.payload.state.index, ctx->msg.payload.state.num);

    if (vq->kickfd >= 0)
        close(vq->kickfd);

    vq->kickfd = VIRTIO_INVALID_EVENTFD;

    if (vq->callfd >= 0)
        close(vq->callfd);

    vq->callfd = VIRTIO_INVALID_EVENTFD;

    ctx->msg.size = sizeof(ctx->msg.payload.state);
    ctx->fd_num = 0;

    vring_invalidate(dev, vq);
    atomic_store_explicit(&vq->started, false, memory_order_release);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_vring_kick(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_file file;
    struct vhost_virtqueue *vq;

    file.index = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
    if (ctx->msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK)
        file.fd = VIRTIO_INVALID_EVENTFD;
    else
        file.fd = ctx->fds[0];
    pr_debug("vring kick idx:%d file:%d\n", file.index, file.fd);

    vq = dev->virtqueue[file.index];
    if (!vq) {
        pr_err("Invalid virtqueue pointer for index %d\n", file.index);
        return VHOST_MSG_RESULT_ERR;
    }

    if (vq->started) {
        pr_err("vq has started, please reset the ring before using new fd\n");
        return VHOST_MSG_RESULT_ERR;
    }
    vq->kickfd = file.fd;

    atomic_store_explicit(&vq->started, true, memory_order_release);

    if (vq->kickfd != VIRTIO_INVALID_EVENTFD) {
        if (vhost_user_set_vring_state(dev, vq, 1) < 0) {
            pr_err("failed to enable the vring!! \n");
            return VHOST_MSG_RESULT_ERR;
        }
    }

    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_call(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    struct vhost_vring_file file;
    struct vhost_virtqueue *vq;

    file.index = ctx->msg.payload.u64 & VHOST_USER_VRING_IDX_MASK;
    if (ctx->msg.payload.u64 & VHOST_USER_VRING_NOFD_MASK)
        file.fd = VIRTIO_INVALID_EVENTFD;
    else
        file.fd = ctx->fds[0];
    pr_debug("vring call idx:%d file:%d\n", file.index, file.fd);

    vq = dev->virtqueue[file.index];
    if (!vq) {
        pr_err("Invalid virtqueue pointer for index %d\n", file.index);
        return VHOST_MSG_RESULT_ERR;
    }

    if (vq->callfd >= 0)
        close(vq->callfd);

    vq->callfd = file.fd;

    return VHOST_MSG_RESULT_OK;
}

static bool
support_protocol_feature(struct vhost_user_dev *dev)
{
    if (dev && dev->dev_ops && dev->dev_ops->get_features) {
        if (dev->dev_ops->get_features(dev) & (1UL << VHOST_USER_F_PROTOCOL_FEATURES))
            return true;
    }
    return false;
}

/*
 * The protocol features that we support are requested.
 */
static int
vhost_user_get_protocol_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = 0;

    if (support_protocol_feature(dev) && dev->dev_ops->get_protocol_features) {
        features = dev->dev_ops->get_protocol_features(dev);
    } else {
        pr_err("failed to get protocol features\n");
        return VHOST_MSG_RESULT_ERR;
    }

    ctx->msg.payload.u64 = features;
    ctx->msg.size = sizeof(ctx->msg.payload.u64);
    ctx->fd_num = 0;
    pr_debug("get protocol features %lx \n", ctx->msg.payload.u64);

    return VHOST_MSG_RESULT_REPLY;
}

static int
vhost_user_set_protocol_features(struct vhost_user_dev *dev,
            struct vhu_msg_context *ctx)
{
    uint64_t features = ctx->msg.payload.u64;

    if (support_protocol_feature(dev) && dev->dev_ops->set_protocol_features) {
        dev->dev_ops->set_protocol_features(dev, features);
    } else {
        pr_err("failed to set protocol features\n");
        return VHOST_MSG_RESULT_ERR;
    }

    pr_debug("set protocol features: 0x%lx\n", features);
    return VHOST_MSG_RESULT_OK;
}

static int
vhost_user_set_vring_enable(struct vhost_user_dev *dev,
        struct vhu_msg_context *ctx)
{
    /* we do not maintain the enbale and disable status for vring, just ignore this.
    * the vring is started and enabled when VHOST_USER_SET_VRING_KICK is processed.
    * the vring is stopped and disabled when VHOST_USER_GET_VRING_BASE is processed.
    * If there is more complicated case, we will add support.
    */
    return VHOST_MSG_RESULT_OK;
}

#define VHOST_MESSAGE_HANDLER(id, handler, accepts_fd) \
    [id] = { #id, handler, accepts_fd },


#define VHOST_MESSAGE_HANDLERS \
VHOST_MESSAGE_HANDLER(VHOST_USER_NONE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_FEATURES, vhost_user_get_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_FEATURES, vhost_user_set_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_OWNER, vhost_user_set_owner, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_RESET_OWNER, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_MEM_TABLE, vhost_user_set_mem_table, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_LOG_BASE, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_LOG_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_NUM, vhost_user_set_vring_num, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ADDR, vhost_user_set_vring_addr, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_BASE, vhost_user_set_vring_base, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_VRING_BASE, vhost_user_get_vring_base, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_KICK, vhost_user_set_vring_kick, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_CALL, vhost_user_set_vring_call, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ERR, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_PROTOCOL_FEATURES, vhost_user_get_protocol_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_PROTOCOL_FEATURES, vhost_user_set_protocol_features, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_QUEUE_NUM, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ENABLE, vhost_user_set_vring_enable, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SEND_RARP, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_NET_SET_MTU, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_BACKEND_REQ_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_IOTLB_MSG, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_VRING_ENDIAN, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_CONFIG, vhost_user_get_config, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_CONFIG, vhost_user_set_config, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_CRYPTO_CREATE_SESS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_CRYPTO_CLOSE_SESS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_ADVISE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_LISTEN, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_POSTCOPY_END, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_INFLIGHT_FD, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_INFLIGHT_FD, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GPU_SET_SOCKET, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_RESET_DEVICE, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_VRING_KICK, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_MAX_MEM_SLOTS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_ADD_MEM_REG, NULL, true) \
VHOST_MESSAGE_HANDLER(VHOST_USER_REM_MEM_REG, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_SET_STATUS, NULL, false) \
VHOST_MESSAGE_HANDLER(VHOST_USER_GET_STATUS, NULL, false)


static vhost_message_handler_t vhost_message_handlers[] = {
    VHOST_MESSAGE_HANDLERS
};
#undef VHOST_MESSAGE_HANDLER

static int
send_vhost_reply(struct vhost_user_dev *dev, int sockfd, struct vhu_msg_context *ctx)
{
    if (!ctx)
        return 0;

    ctx->msg.flags &= ~VHOST_USER_VERSION_MASK;
    ctx->msg.flags &= ~VHOST_USER_NEED_REPLY;
    ctx->msg.flags |= VHOST_USER_VERSION;
    ctx->msg.flags |= VHOST_USER_REPLY_MASK;

    return send_fd_message(sockfd, (char *)&ctx->msg,
        VHOST_USER_HDR_SIZE + ctx->msg.size, ctx->fds, ctx->fd_num);
}

static void
close_msg_fds(struct vhu_msg_context *ctx)
{
    int i;

    for (i = 0; i < ctx->fd_num; i++) {
        int fd = ctx->fds[i];

        if (fd == -1)
            continue;

        ctx->fds[i] = -1;
        close(fd);
    }
}

static int
read_vhost_message(struct vhost_user_dev *dev, int sockfd, struct  vhu_msg_context *ctx)
{
    int ret;

    ret = read_fd_message(sockfd, (char *)&ctx->msg, VHOST_USER_HDR_SIZE,
        ctx->fds, VHOST_MEMORY_MAX_NREGIONS, &ctx->fd_num);
    if (ret <= 0)
        goto out;

    if (ret != VHOST_USER_HDR_SIZE) {
        pr_err("Unexpected header size read\n");
        ret = -1;
        goto out;
    }

    if ((ctx->msg.flags & VHOST_USER_VERSION_MASK) != VHOST_USER_VERSION) {
        pr_err("Unmatched version, wish %d, got %d \n", VHOST_USER_VERSION,
                ctx->msg.flags & VHOST_USER_VERSION_MASK);
        ret = -1;
        goto out;
    }
    if (ctx->msg.size) {
        if (ctx->msg.size > sizeof(ctx->msg.payload)) {
            pr_err("invalid msg size: %d\n",
                ctx->msg.size);
            ret = -1;
            goto out;
        }
        ret = read(sockfd, &ctx->msg.payload, ctx->msg.size);
        if (ret <= 0)
            goto out;
        if (ret != (int)ctx->msg.size) {
            pr_err("read control message failed\n");
            ret = -1;
            goto out;
        }
    }

out:
    if (ret <= 0)
        close_msg_fds(ctx);

    return ret;
}

static int
vhost_user_msg_handler(struct vhost_user_dev *dev, uint32_t fd)
{
    struct vhu_msg_context ctx;
    vhost_message_handler_t *msg_handler;
    int msg_result = VHOST_MSG_RESULT_OK;
    int ret;
    bool handled;
    uint32_t request;

    if (dev == NULL || fd == 0)
        return -1;

    ctx.msg.request.frontend = VHOST_USER_NONE;
    ret = read_vhost_message(dev, fd, &ctx);
    if (ret <= 0) {
        pr_info("vhost peer closed\n");
        return -1;
    }

    request = ctx.msg.request.frontend;
    if (request > VHOST_USER_NONE && request < VHOST_USER_MAX)
        msg_handler = &vhost_message_handlers[request];
    else
        msg_handler = NULL;

    if (ret < 0) {
        pr_err("vhost read message %s%s%sfailed\n",
                msg_handler != NULL ? "for " : "",
                msg_handler != NULL ? msg_handler->description : "",
                msg_handler != NULL ? " " : "");
        return -1;
    }

    if (msg_handler != NULL && msg_handler->description != NULL) {
        pr_debug("read message %s\n",
            msg_handler->description);
    }
    ret = vhost_user_check_and_alloc_queue_pair(dev, &ctx);
    if (ret < 0) {
        pr_err("failed to alloc queue\n");
        return -1;
    }

    handled = false;

    if (msg_handler == NULL || msg_handler->callback == NULL)
        goto err;

    msg_result = msg_handler->callback(dev, &ctx);


    switch (msg_result) {
    case VHOST_MSG_RESULT_ERR:
        pr_err("process failed.\n");
        handled = true;
        break;
    case VHOST_MSG_RESULT_OK:
        pr_debug("process succeeded.\n");
        handled = true;
        break;
    case VHOST_MSG_RESULT_REPLY:
        pr_debug("processing succeeded and needs reply.\n");
        send_vhost_reply(dev, fd, &ctx);
        handled = true;
        break;
    default:
        break;
    }
err:
    /* If message was not handled at this stage, treat it as an error */
    if (!handled) {
        pr_err("vhost message (req: %d) was not handled.\n",
            request);
        close_msg_fds(&ctx);
        msg_result = VHOST_MSG_RESULT_ERR;
    }

    if (msg_result == VHOST_MSG_RESULT_ERR) {
        pr_err("vhost message handling failed.\n");
        return -1;
    }

    return 0;
}

int
vhost_user_init_device(struct vhost_user_dev *dev, struct vhost_dev_ops *dev_ops)
{

    if (!dev || !dev_ops) {
        pr_err("please provide structure dev and dev_ops \n");
        return -1;
    }
    dev->dev_ops = dev_ops;
    return 0;
}

static void
free_mem_region(struct vhost_user_dev *dev)
{
	uint32_t i;
	struct vhost_mem_region *reg;

	if (!dev || !dev->mem)
		return;

	for (i = 0; i < dev->mem->nregions; i++) {
		reg = &dev->mem->regions[i];
		if (reg->host_user_addr) {
			if (reg->mmap_addr) {
				munmap(reg->mmap_addr, reg->mmap_size);
			}
			if (reg->fd >= 0) {
				close(reg->fd);
			}
		}
	}
}

static void
cleanup_vq(struct vhost_virtqueue *vq)
{
    if (vq->callfd >= 0)
        close(vq->callfd);
    if (vq->kickfd >= 0)
        close(vq->kickfd);
    vq->desc = NULL;
    vq->used = NULL;
    vq->avail = NULL;
}

void
vhost_user_deinit_device(struct vhost_user_dev *dev)
{
	uint32_t i;
    struct vhost_virtqueue *vq;

    if (!dev) {
        return;
    }

    if (dev->vsocket.socket_fd >= 0) {
        close(dev->vsocket.socket_fd);
        dev->vsocket.socket_fd = -1;
    }

	for (i = 0; i < dev->nr_vring; i++) {
        vq = dev->virtqueue[i];
        if (vq) {
            vhost_user_set_vring_state(dev, vq, 0);
            cleanup_vq(vq);
            free(vq);
            dev->virtqueue[i] = NULL;
        }
	}

    dev->nr_vring = 0;

    if (dev->mem) {
        free_mem_region(dev);
        free(dev->mem);
        dev->mem = NULL;
	}
    pr_debug("deinit device\n");
}

int
vhost_user_wait_for_connect(struct vhost_user_dev *dev, char *socket_path)
{
    if (create_unix_socket(&dev->vsocket, socket_path) < 0) {
        pr_err("failed to create socket\n");
        return -1;
    }
    if (vhost_user_start_server(&dev->vsocket) < 0) {
        pr_err("failed to setup connection \n");
        return -1;
    }
    return 0;
}

int
vhost_user_start_loop(struct vhost_user_dev *dev)
{
    int ret;

    do {
        ret = vhost_user_msg_handler(dev, dev->vsocket.socket_fd);
        if (ret < 0)
            break;
    } while(1);

    return ret;
}
