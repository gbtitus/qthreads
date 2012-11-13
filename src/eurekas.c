#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* The API */
#include "qthread/qthread.h"

/* System Headers */

/* Internal Headers */
#include "qt_teams.h"
#include "qt_qthread_struct.h"
#include "qt_qthread_mgmt.h"
#include "qthread_innards.h"

TLS_DECL_INIT(uint_fast8_t, eureka_block);
TLS_DECL_INIT(uint_fast8_t, eureka_blocked_flag);

/* Static Variables */
static saligned_t eureka_flag        = -1;
static qt_team_t *eureka_ptr         = NULL;
static aligned_t  eureka_in_barrier  = 0;
static aligned_t  eureka_out_barrier = 0;

static int eureka_filter(qthread_t *t)
{
    if (t->team == eureka_ptr) {
        tassert((t->flags & QTHREAD_REAL_MCCOY) == 0);
        return 2; // remove, keep going
    } else {
        return 0; // ignore, keep going
    }
}

static void eureka(void)
{   /*{{{*/
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    qthread_worker_t   *w = qthread_internal_getworker();
    qthread_shepherd_t *s = w->shepherd;
    qthread_t          *t = w->current;
#else
    qthread_shepherd_t *s = qthread_internal_getshep();
    qthread_t          *t = s->current;
#endif

    if (t) {
        if (t->team == eureka_ptr) {
            tassert((t->flags & QTHREAD_REAL_MCCOY) == 0);
            t->thread_state = QTHREAD_STATE_ASSASSINATED;
        }
    }
    /* 4: entry barrier */
    {
        aligned_t tmp = eureka_out_barrier;
        if (qthread_incr(&eureka_in_barrier, 1) + 1 == qlib->nshepherds) {
            eureka_in_barrier = 0;
            MACHINE_FENCE;
            eureka_out_barrier++;
        } else {
            COMPILER_FENCE;
            while (tmp == eureka_out_barrier) SPINLOCK_BODY();
        }
    }
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    /* 5: worker 0 filters the work queue */
    if (w->worker_id == 0) {
        /* filter work queue! */
        qt_threadqueue_filter(s->ready, eureka_filter);
    }
#else
    qt_threadqueue_filter(s->ready, eureka_filter);
#endif
#ifdef QTHREAD_USE_SPAWNCACHE
    /* 5b: filter the spawncache! */
    qt_spawncache_filter(eureka_filter);
#endif
    /* 7: exit barrier */
    {
        int barrier_participation = 1;
        if (t) {
            if (t->thread_state == QTHREAD_STATE_ASSASSINATED) {
                if (t->rdata->criticalsect > 0) {
                    barrier_participation = 0;
                }
            }
        }
        if (barrier_participation == 1) {
            aligned_t tmp = eureka_out_barrier;
            if (qthread_incr(&eureka_in_barrier, 1) + 1 == qlib->nshepherds) {
                eureka_in_barrier = 0;
                MACHINE_FENCE;
                eureka_out_barrier++;
            } else {
                COMPILER_FENCE;
                while (tmp == eureka_out_barrier) SPINLOCK_BODY();
            }
        }
    }
    if (t) {
        if (t->thread_state == QTHREAD_STATE_ASSASSINATED) {
            tassert(t->rdata);
            if (t->rdata->criticalsect == 0) {
                qthread_back_to_master2(t);
            }
        }
    }
} /*}}}*/

static void hup_handler(int sig)
{   /*{{{*/
    switch(sig) {
        case QT_ASSASSINATE_SIGNAL:
        {
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
            qthread_worker_t   *w = qthread_internal_getworker();
            qthread_shepherd_t *s = w->shepherd;
            qthread_t          *t = w->current;
#else
            qthread_shepherd_t *s = qthread_internal_getshep();
            qthread_t          *t = s->current;
#endif

            tassert(t);
            tassert(t->rdata);
            tassert((t->flags & QTHREAD_REAL_MCCOY) == 0);
            t->thread_state = QTHREAD_STATE_ASSASSINATED;
            qthread_back_to_master2(t);
            break;
        }
        case QT_EUREKA_SIGNAL:
            if (eureka_block) {
                eureka_blocked_flag = 1;
                return;
            }
            eureka();
    }
} /*}}}*/

void INTERNAL qt_eureka_end_criticalsect_dead(qthread_t *self)
{   /*{{{*/
    aligned_t tmp = eureka_out_barrier;

    if (qthread_incr(&eureka_in_barrier, 1) + 1 == qlib->nshepherds) {
        eureka_in_barrier = 0;
        MACHINE_FENCE;
        eureka_out_barrier++;
    } else {
        COMPILER_FENCE;
        while (tmp == eureka_out_barrier) SPINLOCK_BODY();
    }
    qthread_back_to_master2(self);
} /*}}}*/

void INTERNAL qt_eureka_shepherd_init(void)
{   /*{{{*/
    signal(QT_ASSASSINATE_SIGNAL, hup_handler);
    signal(QT_EUREKA_SIGNAL, hup_handler);
} /*}}}*/

static void qthread_internal_team_eureka_febdeath(const qt_key_t      addr,
                                                  qthread_addrstat_t *m,
                                                  void               *arg)
{                                    /*{{{*/
    QTHREAD_FASTLOCK_LOCK(&m->lock); // should be unnecessary
    for (int i = 0; i < 3; i++) {
        qthread_addrres_t *curs, **base;
        switch (i) {
            case 0: curs = m->EFQ; base = &m->EFQ; break;
            case 1: curs = m->FEQ; base = &m->FEQ; break;
            case 2: curs = m->FFQ; base = &m->FFQ; break;
        }
        while (curs) {
            qthread_t *t = curs->waiter;
            switch(eureka_filter(t)) {
                case 0: // ignore, move to the next one
                    base = &curs->next;
                    break;
                case 2: // remove, move to the next one
                {
                    qthread_internal_assassinate(t);
                    *base = curs->next;
                    FREE_ADDRRES(curs);
                    break;
                }
                default:
                    QTHREAD_TRAP();
            }
            curs = curs->next;
        }
    }
    QTHREAD_FASTLOCK_UNLOCK(&m->lock);
} /*}}}*/

void INTERNAL qthread_internal_assassinate(qthread_t *t)
{   /*{{{*/
    assert(t);
    assert((t->flags & QTHREAD_REAL_MCCOY) == 0);

    qthread_debug(THREAD_BEHAVIOR, "thread %i assassinated\n", t->thread_id);
    /* need to clean up return value */
    if (t->ret) {
        if (t->flags & QTHREAD_RET_IS_SYNCVAR) {
            qassert(qthread_syncvar_fill((syncvar_t *)t->ret), QTHREAD_SUCCESS);
        } else {
            qthread_debug(FEB_DETAILS, "tid %u assassinated, filling retval (%p)\n", t->thread_id, t->ret);
            qassert(qthread_fill((aligned_t *)t->ret), QTHREAD_SUCCESS);
        }
    }
    /* we can remove the stack etc. */
    qthread_thread_free(t);
} /*}}}*/

void API_FUNC qt_team_critical_section(qt_team_critical_section_t boundary)
{   /*{{{*/
    assert(qthread_library_initialized);

    qthread_t *self = qthread_internal_self();
    int        critical;

    assert(self);
    switch(boundary) {
        case BEGIN:
            self->rdata->criticalsect++;
            break;
        case END:
            assert(self->rdata->criticalsect > 0);
            if ((--(self->rdata->criticalsect) == 0) &&
                (self->thread_state == QTHREAD_STATE_ASSASSINATED)) {
                qt_eureka_end_criticalsect_dead(self);
            }
            break;
    }
    MACHINE_FENCE;
} /*}}}*/

int API_FUNC qt_team_eureka(void)
{   /*{{{*/
    saligned_t my_wkrid = -1;
    qthread_t *self     = qthread_internal_self();
    qt_team_t *my_team;

#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    qthread_worker_t *wkr = qthread_internal_getworker();
#else
    qthread_shepherd_t *my_shep = qthread_internal_getshep();
#endif

    assert(qthread_library_initialized);
    qassert_ret(qlib != NULL, QTHREAD_NOT_ALLOWED);
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    qassert_ret(wkr != NULL, QTHREAD_NOT_ALLOWED);
#endif
    qassert_ret(self && self->team, QTHREAD_NOT_ALLOWED);
    my_team = self->team;
    /* calling a eureka from outside qthreads makes no sense */
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    my_wkrid = wkr->unique_id;
#else
    my_wkrid = self->rdata->shepherd_ptr->shepherd_id;
#endif
    /* 1: race to see who wins the eureka */
    // qthread_debug(0, "trying to win the eureka contest in my team...\n");
    do {
        while (my_team->eureka_lock != 0) SPINLOCK_BODY();
    } while (qthread_cas(&my_team->eureka_lock, 0, 1) != 0);
    // qthread_debug(0,"trying to win the global eureka contest...\n");
    do {
        while (eureka_flag != -1) SPINLOCK_BODY();
    } while (qthread_cas(&eureka_flag, -1, my_wkrid) != -1);
    // qthread_debug(0,"I (%u) won the eureka contest!\n", my_wkrid);
    MACHINE_FENCE;
    eureka_ptr = my_team;
    MACHINE_FENCE;
    /* 2: writeEF my team's Eureka, to signal all the waiters */
    qthread_writeEF_const(&my_team->eureka, TEAM_SIGNAL_EUREKA(my_team->team_id));
    /* 3: broadcast signal to all the other workers */
    /* NOTE: From here until the end of barrier 2, printfs are STRICTLY
     *    FORBIDDEN. Printf, on many platforms, uses a mutex for one reason or
     *    another (e.g. to lock the output buffer). If the user code receiving
     *    the signal got interrupted while holding that lock, attempting a
     *    printf in this thread will cause deadlock. */
    qthread_shepherd_t *sheps       = qlib->shepherds;
    int                 signalcount = 0;
    for (qthread_shepherd_id_t shep = 0; shep < qlib->nshepherds; shep++) {
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
        qthread_worker_t *wkrs = sheps[shep].workers;
        for (unsigned int wkrid = 0; wkrid < qlib->nworkerspershep; wkrid++) {
            int ret;
            if (wkr == &wkrs[wkrid]) {
                continue;
            }
            signalcount++;
            if ((ret = pthread_kill(wkrs[wkrid].worker, QT_EUREKA_SIGNAL)) != 0) {
                qthread_debug(ALWAYS_OUTPUT, "pthread_kill failed: %s\n", strerror(ret));
                QTHREAD_TRAP(); // cannot be abort, because we're called from a signal handler, and abort makes things go WEIRD
            }
        }
#else   /* ifdef QTHREAD_MULTITHREADED_SHEPHERDS */
        if (&qlib->shepherds[shep] == my_shep) {
            continue;
        }
        signalcount++;
        int ret = pthread_kill(qlib->shepherds[shep].shepherd, QT_EUREKA_SIGNAL);
        if ((ret != 0) && (ret != ESRCH)) {
            qthread_debug(ALWAYS_OUTPUT, "pthread_kill (shep:%i) failed: %i:%s\n", (int)shep, ret, strerror(ret));
            QTHREAD_TRAP(); // cannot be abort, because we're called from a signal handler, and abort makes things go WEIRD
        }
#endif  /* ifdef QTHREAD_MULTITHREADED_SHEPHERDS */
    }
    tassert(signalcount == (qlib->nshepherds * qlib->nworkerspershep) - 1);
    /* 4: entry barrier */
    {
        aligned_t tmp = eureka_out_barrier;
        MACHINE_FENCE;
        if (qthread_incr(&eureka_in_barrier, 1) + 1 == qlib->nshepherds) {
            eureka_in_barrier = 0;
            MACHINE_FENCE;
            eureka_out_barrier++;
        } else {
            COMPILER_FENCE;
            while (tmp == eureka_out_barrier) SPINLOCK_BODY();
        }
    }
    /* 5: worker 0 filters the work queue */
#ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    if (wkr->worker_id == 0) {
        /* filter work queue! */
        qt_threadqueue_filter(self->rdata->shepherd_ptr->ready, eureka_filter);
    }
#else
    qt_threadqueue_filter(self->rdata->shepherd_ptr->ready, eureka_filter);
#endif
#ifdef QTHREAD_USE_SPAWNCACHE
    /* 5b: filter the spawncache! */
    qt_spawncache_filter(eureka_filter);
#endif
    /* 6: callback to kill blocked tasks */
    for (unsigned int i = 0; i < QTHREAD_LOCKING_STRIPES; i++) {
        qt_hash_callback(qlib->FEBs[i],
                         (qt_hash_callback_fn)qthread_internal_team_eureka_febdeath,
                         NULL);
        qt_hash_callback(qlib->syncvars[i],
                         (qt_hash_callback_fn)qthread_internal_team_eureka_febdeath,
                         NULL);
    }
    /* 7: exit barrier */
    {
        aligned_t tmp = eureka_out_barrier;
        if (qthread_incr(&eureka_in_barrier, 1) + 1 == qlib->nshepherds) {
            eureka_in_barrier = 0;
            MACHINE_FENCE;
            eureka_out_barrier++;
        } else {
            COMPILER_FENCE;
            while (tmp == eureka_out_barrier) SPINLOCK_BODY();
        }
    }
    /* 8: release eureka lock */
    MACHINE_FENCE;
    eureka_ptr = NULL;
    MACHINE_FENCE;
    eureka_flag          = -1;
    my_team->eureka_lock = 0;
    /* 9: wait for subteams to die, and reset team (assume team-leader position) */
    /* 9-step1: assume team-leader position */
    self->flags |= QTHREAD_TEAM_LEADER;
    /* 9-step2: reset team data */
    qt_sinc_reset(my_team->sinc, 1);              // I am the only remaining member (and maybe the waiter)
    qt_sinc_submit(my_team->subteams_sinc, NULL); // wait for subteams to die (if any)
    qt_sinc_wait(my_team->subteams_sinc, NULL);
    qt_sinc_reset(my_team->subteams_sinc, 1); // reset the subteams sinc
    /* 9-step3: change my retloc */
    assert(self->ret == NULL || self->ret == my_team->return_loc); // XXX: what should we do if this is not true?
    self->ret    = my_team->return_loc;
    self->flags |= QTHREAD_RET_MASK;
    self->flags ^= QTHREAD_RET_MASK;
    self->flags |= (my_team->flags & QTHREAD_TEAM_RET_MASK);
} /*}}}*/

void INTERNAL qt_eureka_check(int block)
{   /*{{{*/
    if (TLS_GET(eureka_blocked_flag)) {
        TLS_SET(eureka_blocked_flag, 0);
        TLS_SET(eureka_block, 0);
        hup_handler(QT_EUREKA_SIGNAL);
        if (block) {
            TLS_SET(eureka_block, 1);
        }
    } else if (!block) {
        TLS_SET(eureka_block, 0);
    }
} /*}}}*/

void INTERNAL qt_eureka_enable(void)
{   /*{{{*/
    TLS_SET(eureka_block, 0);
} /*}}}*/

void INTERNAL qt_eureka_disable(void)
{   /*{{{*/
    TLS_SET(eureka_block, 1);
} /*}}}*/

/* vim:set expandtab: */