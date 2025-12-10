/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2013  Chris Torek <torek @ torek net>
 * All rights reserved.
 * Copyright (c) 2019 Joyent, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/uio.h>
#include <sys/eventfd.h>
#include "vu_vq.h"
#include "vhost_user.h"
#include "vu_atomic.h"

struct vhost_user_dev;

static int log_info = 1;
static int log_debug;

#define pr_info(fmt, args...) do {if (log_info)  printf(fmt, ##args);} while(0)
#define pr_debug(fmt, args...) do {if (log_debug)  printf(fmt, ##args);} while(0)
#define pr_err(fmt, args...) do { printf(fmt, ##args);} while(0)

int
vq_has_data(struct vhost_virtqueue *vq)
{
    if (!vq) {
        pr_err("Invalid virtqueue pointer in vq_has_data\n");
        return 0;
    }

    if (!vq->avail) {
        pr_err("Invalid avail ring pointer in vq_has_data\n");
        return 0;
    }

    atomic_thread_fence();
    if (vq->started && vq->avail->idx != vq->last_avail_idx)
        return 1;
    return 0;
}

static uint64_t
gpa_to_vva(struct vhost_user_dev *dev, uint64_t gpa, uint64_t len)
{
    if (!dev) {
        pr_err("Invalid dev pointer in gpa_to_vva\n");
        return 0;
    }

    if (!dev->mem) {
        pr_err("Invalid mem pointer in gpa_to_vva\n");
        return 0;
    }

    return vhost_va_from_guest_pa(dev->mem, gpa, len);
}


/*
 * Helper inline for vq_getchain(): record the i'th "real"
 * descriptor.
 */
static inline void
_vq_record(int i, struct virtq_desc *vd, struct vhost_user_dev *dev, struct iovec *iov,
    int n_iov)
{
    if (i >= n_iov || !vd || !dev || !iov)
        return;

    iov[i].iov_base = (void *)gpa_to_vva(dev, vd->addr, vd->len);
    iov[i].iov_len = vd->len;
}


/*
 * Examine the chain of descriptors starting at the "next one" to
 * make sure that they describe a sensible request.  If so, return
 * the number of "real" descriptors that would be needed/used in
 * acting on this request.  This may be smaller than the number of
 * available descriptors, e.g., if there are two available but
 * they are two separate requests, this just returns 1.  Or, it
 * may be larger: if there are indirect descriptors involved,
 * there may only be one descriptor available but it may be an
 * indirect pointing to eight more.  We return 8 in this case,
 * i.e., we do not count the indirect descriptors, only the "real"
 * ones.
 *
 * Basically, this vets the "flags" and "next" field of each
 * descriptor and tells you how many are involved.  Since some may
 * be indirect, this also needs the vmctx (in the pci_devinst
 * at vs->vs_pi) so that it can find indirect descriptors.
 *
 * As we process each descriptor, we copy and adjust it (guest to
 * host address wise, also using the vmtctx) into the given iov[]
 * array (of the given size).  If the array overflows, we stop
 * placing values into the array but keep processing descriptors,
 * up to VQ_MAX_DESCRIPTORS, before giving up and returning -1.
 * So you, the caller, must not assume that iov[] is as big as the
 * return value (you can process the same thing twice to allocate
 * a larger iov array if needed, or supply a zero length to find
 * out how much space is needed).
 *
 * If some descriptor(s) are invalid, this prints a diagnostic message
 * and returns -1.  If no descriptors are ready now it simply returns 0.
 */
int
vq_getchain(struct vhost_virtqueue *vq, struct iovec *iov, int niov, int *ridx)
{
    int i;
    uint32_t ndesc;
    uint32_t idx, next;
    struct virtq_desc *vdir;
    struct vhost_user_dev *dev;

    if (!vq || !ridx) {
        pr_err("Invalid vq or ridx pointer in vq_getchain\n");
        return -1;
    }

    dev = vq->vudev;
    if (!dev) {
        pr_err("Invalid dev pointer in vq_getchain\n");
        return -1;
    }

    if (!vq->avail || !vq->desc) {
        pr_err("Invalid avail or desc ring pointer in vq_getchain\n");
        return -1;
    }

    /*
     * Note: it's the responsibility of the guest not to
     * update vq->vq_avail->idx until all of the descriptors
     * the guest has written are valid (including all their
     * "next" fields and "flags").
     *
     * Compute (vq_avail->idx - last_avail) in integers mod 2**16.  This is
     * the number of descriptors the device has made available
     * since the last time we updated vq->last_avail_idx.
     *
     * We just need to do the subtraction as an unsigned int,
     * then trim off excess bits.
     */
    idx = vq->last_avail_idx;
    ndesc = (uint16_t)((uint32_t)vq->avail->idx - idx);
    if (ndesc == 0)
        return 0;
    if (ndesc > vq->size) {
        /* XXX need better way to diagnose issues */
        pr_err("ndesc (%u) out of range, driver confused?", (uint32_t)ndesc);
        return -1;
    }

    /*
     * Now count/parse "involved" descriptors starting from
     * the head of the chain.
     *
     * To prevent loops, we could be more complicated and
     * check whether we're re-visiting a previously visited
     * index, but we just abort if the count gets excessive.
     */
    *ridx = next = vq->avail->ring[idx & (vq->size - 1)];
    vq->last_avail_idx++;
    for (i = 0; i < niov; next = vdir->next) {
        if (next >= vq->size) {
            pr_err(" descriptor index %u out of range, "
                    "driver confused?", next);
            return -1;
        }
        vdir = &vq->desc[next];
        if (dev->negotiated_feats & (1UL << VIRTIO_F_INDIRECT_DESC))
        {
            pr_err("ERROR, not support indirect description\n");
        } else {
            if (iov) {
                _vq_record(i, vdir, dev, iov, niov);
            }
            i++;
        }
        if ((vdir->flags & VIRTQ_DESC_F_NEXT) == 0)
            break;
    }
    pr_debug("%s: get %d requests from virtqueue idx %d \n", __func__, i, *ridx);
    return i;
}

/*
 * Return the first n_chain request chains back to the available queue.
 *
 * (These chains are the ones you handled when you called vq_getchain()
 * and used its positive return value.)
 */
void
vq_retchains(struct vhost_virtqueue *vq, uint16_t n_chains)
{
    if (!vq) {
        pr_err("Invalid virtqueue pointer in vq_retchains\n");
        return;
    }

    vq->last_avail_idx -= n_chains;
}

/*
 * Return specified request chain to the guest, setting its I/O length
 * to the provided value.
 *
 * (This chain is the one you handled when you called vq_getchain()
 * and used its positive return value.)
 */
void
vq_relchain(struct vhost_virtqueue *vq, uint16_t idx, uint32_t iolen)
{
    struct virtq_used *vuh;
    struct virtq_used_elem *vue;
    uint16_t mask;

    if (!vq) {
        pr_err("Invalid virtqueue pointer in vq_relchain\n");
        return;
    }

    if (!vq->used) {
        pr_err("Invalid used ring pointer in vq_relchain\n");
        return;
    }

    /*
     * Notes:
     *  - mask is N-1 where N is a power of 2 so computes x % N
     *  - vuh points to the "used" data shared with guest
     *  - vue points to the "used" ring entry we want to update
     */
    mask = vq->size - 1;
    vuh = vq->used;
    vue = &vuh->ring[vq->last_used_idx++ & mask];
    vue->id = idx;
    vue->len = iolen;
    smp_mb();
    vq->used->idx = vq->last_used_idx;
}

/*
 * Driver has finished processing "available" chains and calling
 * vq_relchain on each one.  If driver used all the available
 * chains, used_all should be set.
 *
 * If the "used" index moved we may need to inform the guest, i.e.,
 * deliver an interrupt.  Even if the used index did NOT move we
 * may need to deliver an interrupt, if the avail ring is empty and
 * we are supposed to interrupt on empty.
 *
 * Note that used_all_avail is provided by the caller because it's
 * a snapshot of the ring state when he decided to finish interrupt
 * processing -- it's possible that descriptors became available after
 * that point.  (It's also typically a constant 1/True as well.)
 */
void
vq_endchains(struct vhost_virtqueue *vq)
{
    uint16_t event_idx, new_idx, old_idx;
    int intr;
    struct vhost_user_dev *dev;

    if (!vq) {
        pr_err("Invalid virtqueue pointer in vq_endchains\n");
        return;
    }

    dev = vq->vudev;
    if (!dev) {
        pr_err("Invalid dev pointer in vq_endchains\n");
        return;
    }

    if (!vq->used) {
        pr_err("Invalid used ring pointer in vq_endchains\n");
        return;
    }

    /*
     * Interrupt generation: if we're using EVENT_IDX,
     * interrupt if we've crossed the event threshold.
     * Otherwise interrupt is generated if we added "used" entries,
     * but suppressed by VRING_AVAIL_F_NO_INTERRUPT.
     *
     * In any case, though, if NOTIFY_ON_EMPTY is set and the
     * entire avail was processed, we need to interrupt always.
     */
    atomic_thread_fence();
    old_idx = vq->saved_used_idx;
    vq->saved_used_idx = new_idx = vq->used->idx;

    if (dev->negotiated_feats & (1UL << VIRTIO_F_EVENT_IDX)) {
        event_idx = *virtq_used_event(vq);
        /*
        * This calculation is per docs and the kernel
        * (see src/sys/dev/virtio/virtio_ring.h).
        */
        intr = (uint16_t)(new_idx - event_idx - 1) <
            (uint16_t)(new_idx - old_idx);
    } else {
        if (!vq->avail) {
            pr_err("Invalid avail ring pointer in vq_endchains\n");
            return;
        }
        intr = new_idx != old_idx &&
            !(vq->avail->flags & VIRTQ_AVAIL_F_NO_INTERRUPT);
    }
    if (intr) {
        pr_debug("%s: inject irq \n", __func__);
        if (vq->callfd >= 0) {
            if (eventfd_write(vq->callfd, 1) < 0)
                pr_err("Failed to write callfd !\n");
        } else {
            pr_err("Invalid callfd in vq_endchains\n");
        }
    }
}
