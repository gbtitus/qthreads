#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "qt_feb_barrier.h"
#include "qthread_asserts.h"
#include "qt_atomics.h"
#include "qt_mpool.h"
#include <qthread/qthread.h>

struct qt_feb_barrier_s
{
    aligned_t block_on_me;
    aligned_t blockers;
    size_t max_blockers;
};

static qt_mpool feb_barrier_pool = NULL;
static qt_feb_barrier_t *global_barrier = NULL;

static void cleanup_feb_barrier(void)
{
    qt_mpool_destroy(feb_barrier_pool);
    feb_barrier_pool = NULL;
}

qt_feb_barrier_t *qt_feb_barrier_create(qthread_t *me, size_t max_threads)
{
    qt_feb_barrier_t *b;
    if (feb_barrier_pool == NULL) {
	qt_mpool bp = qt_mpool_create(qthread_num_shepherds()>1, sizeof(struct qt_feb_barrier_s), -1);
	if (qt_cas(&feb_barrier_pool, NULL, bp) != NULL) {
	    /* someone else created an mpool first */
	    qt_mpool_destroy(bp);
	} else {
	    atexit(cleanup_feb_barrier);
	}
    }
    b = qt_mpool_alloc(feb_barrier_pool);
    b->blockers = 0;
    b->max_blockers = max_threads;
    qthread_empty(me, &b->block_on_me);
    return b;
}

void qt_feb_barrier_enter(qthread_t *me, qt_feb_barrier_t *b)
{
    aligned_t waiters = qthread_incr(&b->blockers, 1) + 1;
    qassert_retvoid(b);
    printf("waiters = %i, max_blockers = %i\n", (int)waiters, (int)b->max_blockers);
    if (waiters == b->max_blockers) {
	/* last guy into the barrier */
	qthread_fill(me, &b->block_on_me);
	b->blockers = 0;
	qthread_empty(me, &b->block_on_me);
    } else {
	qthread_readFF(me, NULL, &b->block_on_me);
    }
}

void qt_feb_barrier_destroy(qthread_t *me, qt_feb_barrier_t *b)
{
    assert(feb_barrier_pool != NULL);
    assert(b->blockers == 0);
    qthread_fill(me, &b->block_on_me);
    qt_mpool_free(feb_barrier_pool, b);
}

#ifdef QTHREAD_GLOBAL_FEB_BARRIER
void qthread_reset_forCount(qthread_t *);	// KBW
void qt_global_barrier(qthread_t *me)
{
    assert(global_barrier);
    qt_feb_barrier_enter(me, global_barrier);
    //  now execute code on one thread that everyone needs to see -- should be
    //     at middle of barrier but does not seem to work there -- so here with double barrier
    //     blech.  akp -2/9/10
    qthread_reset_forCount(qthread_self());	// for loop reset on each thread
    qt_feb_barrier_enter(me, global_barrier);
    return;
}

// allow barrer initization from C
void qt_global_barrier_init(int size, int debug)
{
    if (global_barrier == NULL) {
	global_barrier = qt_feb_barrier_create(NULL, size);
	assert(global_barrier);
    }
}

void qt_global_barrier_destroy()
{
    if (global_barrier) {
	qt_feb_barrier_destroy(qthread_self(), global_barrier);
	global_barrier = NULL;
    }
}
#endif