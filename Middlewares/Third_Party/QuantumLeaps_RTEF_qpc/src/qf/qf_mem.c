//$file${src::qf::qf_mem.c} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
//
// Model: qpc.qm
// File:  ${src::qf::qf_mem.c}
//
// This code has been generated by QM 6.1.1 <www.state-machine.com/qm>.
// DO NOT EDIT THIS FILE MANUALLY. All your changes will be lost.
//
// This code is covered by the following QP license:
// License #    : LicenseRef-QL-dual
// Issued to    : Any user of the QP/C real-time embedded framework
// Framework(s) : qpc
// Support ends : 2024-12-31
// License scope:
//
// Copyright (C) 2005 Quantum Leaps, LLC <state-machine.com>.
//
//                    Q u a n t u m  L e a P s
//                    ------------------------
//                    Modern Embedded Software
//
// SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-QL-commercial
//
// This software is dual-licensed under the terms of the open source GNU
// General Public License version 3 (or any later version), or alternatively,
// under the terms of one of the closed source Quantum Leaps commercial
// licenses.
//
// The terms of the open source GNU General Public License version 3
// can be found at: <www.gnu.org/licenses/gpl-3.0>
//
// The terms of the closed source Quantum Leaps commercial licenses
// can be found at: <www.state-machine.com/licensing>
//
// Redistributions in source code must retain this top-level comment block.
// Plagiarizing this software to sidestep the license obligations is illegal.
//
// Contact information:
// <www.state-machine.com/licensing>
// <info@state-machine.com>
//
//$endhead${src::qf::qf_mem.c} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#define QP_IMPL           // this is QP implementation
#include "qp_port.h"      // QP port
#include "qp_pkg.h"       // QP package-scope interface
#include "qsafe.h"        // QP Functional Safety (FuSa) Subsystem
#ifdef Q_SPY              // QS software tracing enabled?
    #include "qs_port.h"  // QS port
    #include "qs_pkg.h"   // QS facilities for pre-defined trace records
#else
    #include "qs_dummy.h" // disable the QS software tracing
#endif // Q_SPY

Q_DEFINE_THIS_MODULE("qf_mem")

//$skip${QP_VERSION} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// Check for the minimum required QP version
#if (QP_VERSION < 730U) || (QP_VERSION != ((QP_RELEASE^4294967295U) % 0x3E8U))
#error qpc version 7.3.0 or higher required
#endif
//$endskip${QP_VERSION} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//$define${QF::QMPool} vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

//${QF::QMPool} ..............................................................

//${QF::QMPool::init} ........................................................
//! @public @memberof QMPool
void QMPool_init(QMPool * const me,
    void * const poolSto,
    uint_fast32_t const poolSize,
    uint_fast16_t const blockSize)
{
    QF_CRIT_STAT
    QF_CRIT_ENTRY();
    QF_MEM_SYS();

    Q_REQUIRE_INCRIT(100, (poolSto != (void *)0)
            && (poolSize >= (uint_fast32_t)sizeof(QFreeBlock))
            && ((uint_fast16_t)(blockSize + sizeof(QFreeBlock)) > blockSize));

    me->free_head = (QFreeBlock *)poolSto;

    // find # free blocks in a memory block, NO DIVISION
    me->blockSize = (QMPoolSize)sizeof(QFreeBlock);
    uint_fast16_t nblocks = 1U;
    while (me->blockSize < (QMPoolSize)blockSize) {
        me->blockSize += (QMPoolSize)sizeof(QFreeBlock);
        ++nblocks;
    }

    // the pool buffer must fit at least one rounded-up block
    Q_ASSERT_INCRIT(110, poolSize >= me->blockSize);

    // start at the head of the free list
    QFreeBlock *fb = me->free_head;
    me->nTot = 1U; // the last block already in the list

    // chain all blocks together in a free-list...
    for (uint_fast32_t size = poolSize - me->blockSize;
         size >= (uint_fast32_t)me->blockSize;
         size -= (uint_fast32_t)me->blockSize)
    {
        fb->next = &fb[nblocks]; // point next link to next block
    #ifndef Q_UNSAFE
        fb->next_dis = (uintptr_t)(~Q_UINTPTR_CAST_(fb->next));
    #endif
        fb = fb->next;           // advance to the next block
        ++me->nTot;              // one more free block in the pool
    }

    fb->next  = (QFreeBlock *)0; // the last link points to NULL
    #ifndef Q_UNSAFE
    fb->next_dis = (uintptr_t)(~Q_UINTPTR_CAST_(fb->next));
    #endif

    me->nFree = me->nTot;        // all blocks are free
    me->nMin  = me->nTot;        // the minimum # free blocks
    me->start = (QFreeBlock *)poolSto; // the original start this pool buffer
    me->end   = fb;              // the last block in this pool

    QF_MEM_APP();
    QF_CRIT_EXIT();
}

//${QF::QMPool::get} .........................................................
//! @public @memberof QMPool
void * QMPool_get(QMPool * const me,
    uint_fast16_t const margin,
    uint_fast8_t const qsId)
{
    #ifndef Q_SPY
    Q_UNUSED_PAR(qsId);
    #endif

    QF_CRIT_STAT
    QF_CRIT_ENTRY();
    QF_MEM_SYS();

    // have more free blocks than the requested margin?
    QFreeBlock *fb;
    if (me->nFree > (QMPoolCtr)margin) {
        fb = me->free_head; // get a free block

        //  a free block must be valid
        Q_ASSERT_INCRIT(300, fb != (QFreeBlock *)0);

        QFreeBlock * const fb_next = fb->next; // fast temporary

        // the free block must have integrity (duplicate inverse storage)
        Q_ASSERT_INCRIT(302, Q_UINTPTR_CAST_(fb_next)
                              == (uintptr_t)~fb->next_dis);

        --me->nFree; // one less free block
        if (me->nFree == 0U) { // is the pool becoming empty?
            // pool is becoming empty, so the next free block must be NULL
            Q_ASSERT_INCRIT(320, fb_next == (QFreeBlock *)0);

            me->nMin = 0U; // remember that the pool got empty
        }
        else {
            // invariant:
            // The pool is not empty, so the next free-block pointer,
            // so the next free block must be in range.

            // NOTE: The next free block pointer can fall out of range
            // when the client code writes past the memory block, thus
            // corrupting the next block.
            Q_ASSERT_INCRIT(330,
                (me->start <= fb_next) && (fb_next <= me->end));

            // is the # free blocks the new minimum so far?
            if (me->nMin > me->nFree) {
                me->nMin = me->nFree; // remember the new minimum
            }
        }

        me->free_head = fb_next; // set the head to the next free block

        QS_BEGIN_PRE_(QS_QF_MPOOL_GET, qsId)
            QS_TIME_PRE_();         // timestamp
            QS_OBJ_PRE_(me);        // this memory pool
            QS_MPC_PRE_(me->nFree); // # of free blocks in the pool
            QS_MPC_PRE_(me->nMin);  // min # free blocks ever in the pool
        QS_END_PRE_()
    }
    else { // don't have enough free blocks at this point
        fb = (QFreeBlock *)0;

        QS_BEGIN_PRE_(QS_QF_MPOOL_GET_ATTEMPT, qsId)
            QS_TIME_PRE_();         // timestamp
            QS_OBJ_PRE_(me);        // this memory pool
            QS_MPC_PRE_(me->nFree); // # of free blocks in the pool
            QS_MPC_PRE_(margin);    // the requested margin
        QS_END_PRE_()
    }

    QF_MEM_APP();
    QF_CRIT_EXIT();

    return fb; // return the block or NULL pointer to the caller
}

//${QF::QMPool::put} .........................................................
//! @public @memberof QMPool
void QMPool_put(QMPool * const me,
    void * const block,
    uint_fast8_t const qsId)
{
    #ifndef Q_SPY
    Q_UNUSED_PAR(qsId);
    #endif

    QFreeBlock * const fb = (QFreeBlock *)block;

    QF_CRIT_STAT
    QF_CRIT_ENTRY();
    QF_MEM_SYS();

    Q_REQUIRE_INCRIT(200, (me->nFree < me->nTot)
                           && (me->start <= fb) && (fb <= me->end));

    fb->next = me->free_head; // link into list
    #ifndef Q_UNSAFE
    fb->next_dis = (uintptr_t)(~Q_UINTPTR_CAST_(fb->next));
    #endif

    // set as new head of the free list
    me->free_head = fb;

    ++me->nFree; // one more free block in this pool

    QS_BEGIN_PRE_(QS_QF_MPOOL_PUT, qsId)
        QS_TIME_PRE_();         // timestamp
        QS_OBJ_PRE_(me);        // this memory pool
        QS_MPC_PRE_(me->nFree); // the # free blocks in the pool
    QS_END_PRE_()

    QF_MEM_APP();
    QF_CRIT_EXIT();
}
//$enddef${QF::QMPool} ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
