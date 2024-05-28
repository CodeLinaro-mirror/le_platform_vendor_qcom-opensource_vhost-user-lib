/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

/* Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef _VHOST_USER_H
#define _VHOST_USER_H

#include <stdint.h>
#include <stddef.h>
#include "vu_vq.h"
#include "vu_socket.h"
#include "vu_atomic.h"

#define __packed __attribute__((packed))
/*
 * Macro to return the maximum of two numbers
 */
#define MAX(a, b) \
    __extension__ ({ \
        typeof (a) _a = (a); \
        typeof (b) _b = (b); \
        _a > _b ? _a : _b; \
    })

/** Protocol features. */
#define VHOST_USER_PROTOCOL_F_MQ    0
#define VHOST_USER_PROTOCOL_F_LOG_SHMFD 1
#define VHOST_USER_PROTOCOL_F_RARP  2
#define VHOST_USER_PROTOCOL_F_REPLY_ACK 3
#define VHOST_USER_PROTOCOL_F_NET_MTU   4
#define VHOST_USER_PROTOCOL_F_BACKEND_REQ   5
#define VHOST_USER_PROTOCOL_F_CRYPTO_SESSION 7
#define VHOST_USER_PROTOCOL_F_PAGEFAULT 8
#define VHOST_USER_PROTOCOL_F_CONFIG 9
#define VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD 10
#define VHOST_USER_PROTOCOL_F_HOST_NOTIFIER 11
#define VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD 12
#define VHOST_USER_PROTOCOL_F_STATUS 16
/** Indicate whether protocol features negotiation is supported. */
#define VHOST_USER_F_PROTOCOL_FEATURES  30

#define VHOST_MEMORY_MAX_NREGIONS 8

#define VIRTIO_F_ANY_LAYOUT        27

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_GET_PROTOCOL_FEATURES = 15,
    VHOST_USER_SET_PROTOCOL_FEATURES = 16,
    VHOST_USER_GET_QUEUE_NUM = 17,
    VHOST_USER_SET_VRING_ENABLE = 18,
    VHOST_USER_SEND_RARP = 19,
    VHOST_USER_NET_SET_MTU = 20,
    VHOST_USER_SET_BACKEND_REQ_FD = 21,
    VHOST_USER_IOTLB_MSG = 22,
    VHOST_USER_SET_VRING_ENDIAN = 23,
    VHOST_USER_GET_CONFIG = 24,
    VHOST_USER_SET_CONFIG = 25,
    VHOST_USER_CRYPTO_CREATE_SESS = 26,
    VHOST_USER_CRYPTO_CLOSE_SESS = 27,
    VHOST_USER_POSTCOPY_ADVISE = 28,
    VHOST_USER_POSTCOPY_LISTEN = 29,
    VHOST_USER_POSTCOPY_END = 30,
    VHOST_USER_GET_INFLIGHT_FD = 31,
    VHOST_USER_SET_INFLIGHT_FD = 32,
    VHOST_USER_GPU_SET_SOCKET = 33,
    VHOST_USER_RESET_DEVICE = 34,
    VHOST_USER_VRING_KICK = 35,
    VHOST_USER_GET_MAX_MEM_SLOTS = 36,
    VHOST_USER_ADD_MEM_REG = 37,
    VHOST_USER_REM_MEM_REG = 38,
    VHOST_USER_SET_STATUS = 39,
    VHOST_USER_GET_STATUS = 40,
    VHOST_USER_MAX,
} VhostUserRequest;


typedef enum VhostUserBackendRequest {
    VHOST_USER_BACKEND_NONE = 0,
    VHOST_USER_BACKEND_IOTLB_MSG = 1,
    VHOST_USER_BACKEND_CONFIG_CHANGE_MSG = 2,
    VHOST_USER_BACKEND_VRING_HOST_NOTIFIER_MSG = 3,
} VhostUserBackendRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserVringArea {
    uint64_t u64;
    uint64_t size;
    uint64_t offset;
} VhostUserVringArea;

/* Virtio device status as per Virtio specification */
#define VIRTIO_DEVICE_STATUS_RESET      0x00
#define VIRTIO_DEVICE_STATUS_ACK        0x01
#define VIRTIO_DEVICE_STATUS_DRIVER     0x02
#define VIRTIO_DEVICE_STATUS_DRIVER_OK      0x04
#define VIRTIO_DEVICE_STATUS_FEATURES_OK    0x08
#define VIRTIO_DEVICE_STATUS_DEV_NEED_RESET 0x40
#define VIRTIO_DEVICE_STATUS_FAILED     0x80

#define VHOST_MAX_VRING         0x100
#define VHOST_MAX_QUEUE_PAIRS       0x80

/**
 * Possible results of the vhost user message handling callbacks
 */
enum vhost_msg_result {
    /* Message handling failed */
    VHOST_MSG_RESULT_ERR = -1,
    /* Message handling successful */
    VHOST_MSG_RESULT_OK =  0,
    /* Message handling successful and reply prepared */
    VHOST_MSG_RESULT_REPLY =  1,
    /* Message not handled */
    VHOST_MSG_RESULT_NOT_HANDLED,
};

struct vhost_vring_state {
    uint32_t index;
    uint32_t num;
};

struct vhost_vring_file {
    uint32_t index;
    int fd;
};

// Virtual address in VMM (qcrosvm)
struct vhost_vring_addr {
    uint32_t index;
    /* Option flags. */
    uint32_t flags;
    /* Flag values: */
    /* Whether log address is valid. If set enables logging. */
#define VHOST_VRING_F_LOG 0

    /* Start of array of descriptors (virtually contiguous) */
    uint64_t desc_user_addr;
    /* Used structure address. Must be 32 bit aligned */
    uint64_t used_user_addr;
    /* Available structure address. Must be 16 bit aligned */
    uint64_t avail_user_addr;
};

struct vhost_virtqueue {
    struct virtq_desc   *desc;
    struct virtq_avail  *avail;
    struct virtq_used   *used;
    uint16_t        size;
    uint16_t        last_avail_idx;
    uint16_t        last_used_idx;
#define VIRTIO_INVALID_EVENTFD      (-1)
    bool            started;
    uint16_t        saved_used_idx;
    struct vhost_user_dev   *vudev;

    /* Used to notify the guest (trigger interrupt) */
    int         callfd;
    int         kickfd;

    /* Index of this vq in dev->virtqueue[] */
    uint32_t        index;

    struct vhost_vring_addr ring_addrs;
};

/* Get location of event indices (only with VIRTIO_F_EVENT_IDX) */ 
static inline le16 *virtq_used_event(struct vhost_virtqueue *vq) 
{ 
        /* For backwards compat, used event index is at *end* of avail ring. */ 
        return &vq->avail->ring[vq->size]; 
} 
 
static inline le16 *virtq_avail_event(struct vhost_virtqueue *vq) 
{ 
        /* For backwards compat, avail event index is at *end* of used ring. */ 
        return (le16 *)&vq->used->ring[vq->size]; 
} 

static inline int virtq_need_event(uint16_t event_idx, uint16_t new_idx, uint16_t old_idx) 
{ 
         return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old_idx); 
} 
 
typedef struct VhostUserMsg {
    union {
        uint32_t frontend; /* a VhostUserRequest value */
        uint32_t backend;  /* a VhostUserBackendRequest value*/
    } request;

#define VHOST_USER_VERSION_MASK     0x3
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
#define VHOST_USER_NEED_REPLY       (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
#define VHOST_USER_VRING_IDX_MASK   0xff
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserVringArea area;
    } payload;
    /* Nothing should be added after the payload */
} __packed VhostUserMsg;

/* Note: this structure and VhostUserMsg can't be changed carelessly as
 * external message handlers rely on them.
 */
struct __packed vhu_msg_context {
    VhostUserMsg msg;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    int fd_num;
};

/**
 * Information relating to memory regions including offsets to
 * addresses in VMM's memory file.
 */
struct vhost_mem_region {
    uint64_t guest_phys_addr;
    uint64_t guest_user_addr;
    uint64_t host_user_addr;
    uint64_t size;
    void     *mmap_addr;
    uint64_t mmap_size;
    int fd;
};

/**
 * Memory structure includes region and mapping information.
 */
struct vhost_memory {
    uint32_t nregions;
    struct vhost_mem_region regions[];
};

struct vhost_dev_ops {
    void (*set_features)(struct vhost_user_dev *dev, uint64_t features);
    uint64_t (*get_features)(struct vhost_user_dev *dev);
    int (*set_vring_state)(struct vhost_user_dev *dev, uint32_t idx, uint32_t state);
};

struct vhost_user_dev {
    struct vhost_memory *mem;
    struct vhost_virtqueue  *virtqueue[VHOST_MAX_QUEUE_PAIRS * 2];
    uint8_t         status;
    // record the real vring number which is initiated.
    uint8_t     nr_vring;
    struct vhost_user_socket    vsocket;
    struct vhost_dev_ops *dev_ops;
    uint64_t        negotiated_feats;
};

static uint64_t
vhost_va_from_guest_pa(struct vhost_memory *mem,
                           uint64_t gpa, uint64_t len)
{
    struct vhost_mem_region *r;
    uint32_t i;

    for (i = 0; i < mem->nregions; i++) {
        r = &mem->regions[i];
        if (gpa >= r->guest_phys_addr &&
            gpa < r->guest_phys_addr + r->size) {

            if (len > r->guest_phys_addr + r->size - gpa)
                len = r->guest_phys_addr + r->size - gpa;

            return gpa - r->guest_phys_addr +
                   r->host_user_addr;
        }
    }

    return 0;
}

typedef void (*kick_handler) (void *data);

#define VHOST_USER_HDR_SIZE offsetof(VhostUserMsg, payload.u64)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    0x1

int vhost_user_init_device(struct vhost_user_dev *dev, struct vhost_dev_ops *dev_ops);

void vhost_user_deinit_device(struct vhost_user_dev *dev);

int vhost_user_wait_for_connect(struct vhost_user_dev *dev, char *socket_path);

int vhost_user_start_loop(struct vhost_user_dev *dev);

#endif
