#ifndef QT_IO_H
#define QT_IO_H

#include "stdlib.h"                  /* malloc() and free() */

#include "qt_blocking_structs.h"
#include "qt_qthread_struct.h"

#if defined(UNPOOLED)
# define ALLOC_SYSCALLJOB (qt_blocking_queue_node_t *)malloc(sizeof(qt_blocking_queue_node_t))
# define FREE_SYSCALLJOB(s) free(s)
#else
# define ALLOC_SYSCALLJOB qt_mpool_alloc(syscall_job_pool);
# define FREE_SYSCALLJOB(j) qt_mpool_free(syscall_job_pool, j);
#endif

extern qt_mpool syscall_job_pool;

void qt_blocking_subsystem_init(void);
int  qt_process_blocking_call(void);
void qt_blocking_subsystem_enqueue(qt_blocking_queue_node_t *job);

void qt_blocking_subsystem_begin_blocking_action(void);
void qt_blocking_subsystem_end_blocking_action(void);

#endif // ifndef QT_IO_H
/* vim:set expandtab: */
