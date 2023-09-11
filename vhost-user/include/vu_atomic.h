/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015 Cavium, Inc
 * Copyright(c) 2020 Arm Limited
 */

/* Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _ATOMIC_ARM64_H_
#define _ATOMIC_ARM64_H_

#ifdef ARCH_ARM64
#define mb() asm volatile("dmb osh" : : : "memory")

#define wmb() asm volatile("dmb oshst" : : : "memory")

#define rmb() asm volatile("dmb oshld" : : : "memory")

#define smp_mb() asm volatile("dmb ish" : : : "memory")

#define smp_wmb() asm volatile("dmb ishst" : : : "memory")

#define smp_rmb() asm volatile("dmb ishld" : : : "memory")
#else
#define mb() {}

#define wmb() {}

#define rmb() {}

#define smp_mb() {}

#define smp_wmb() {}

#define smp_rmb() {}

#endif


static inline void
atomic_thread_fence(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

#endif
