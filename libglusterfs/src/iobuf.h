/*
   Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _IOBUF_H_
#define _IOBUF_H_

#include "list.h"
#include "common-utils.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/uio.h>

#define GF_VARIABLE_IOBUF_COUNT 32
#define GF_IOBREF_IOBUF_COUNT 16

/* Lets try to define the new anonymous mapping
 * flag, in case the system is still using the
 * now deprecated MAP_ANON flag.
 *
 * Also, this should ideally be in a centralized/common
 * header which can be used by other source files also.
 */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* one allocatable unit for the consumers of the IOBUF API */
/* each unit hosts @page_size bytes of memory */
struct iobuf;

/* one region of memory MMAPed from the operating system */
/* each region MMAPs @arena_size bytes of memory */
/* each arena hosts @arena_size / @page_size IOBUFs */
struct iobuf_arena;

/* expandable and contractable pool of memory, internally broken into arenas */
struct iobuf_pool;


struct iobuf {
        union {
                struct list_head      list;
                struct {
                        struct iobuf *next;
                        struct iobuf *prev;
                };
        };
        struct iobuf_arena  *iobuf_arena;

        gf_lock_t            lock; /* for ->ptr and ->ref */
        int                  ref;  /* 0 == passive, >0 == active */

        void                *ptr;  /* usable memory region by the consumer */
};


struct iobuf_arena {
        union {
                struct list_head            list;
                struct {
                        struct iobuf_arena *next;
                        struct iobuf_arena *prev;
                };
        };

        size_t              page_size;  /* size of all iobufs in this arena */
        size_t              arena_size; /* this is equal to
                                           (iobuf_pool->arena_size / page_size)
                                           * page_size */

        struct iobuf_pool  *iobuf_pool;

        void               *mem_base;
        struct iobuf       *iobufs;     /* allocated iobufs list */

        int                 active_cnt;
        struct iobuf        active;     /* head node iobuf
                                           (unused by itself) */
        int                 passive_cnt;
        struct iobuf        passive;    /* head node iobuf
                                           (unused by itself) */
        uint64_t            alloc_cnt;  /* total allocs in this pool */
        int                 max_active; /* max active buffers at a given time */
};


struct iobuf_pool {
        pthread_mutex_t     mutex;
        size_t              arena_size; /* size of memory region in
                                           arena */
        size_t              default_page_size; /* default size of iobuf */

        int                 arena_cnt;
        struct list_head    arenas[GF_VARIABLE_IOBUF_COUNT];
        /* array of arenas. Each element of
           the array is a list of arenas
           holding iobufs of particular
           page_size
        */
        struct list_head    filled[GF_VARIABLE_IOBUF_COUNT];
        /*
          array of arenas without free iobufs
        */

        struct list_head    purge[GF_VARIABLE_IOBUF_COUNT];
        /*
          array of of arenas which can be
          purged
        */
};


struct iobuf_pool *iobuf_pool_new (size_t arena_size, size_t page_size);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
struct iobuf *iobuf_get (struct iobuf_pool *iobuf_pool);
void iobuf_unref (struct iobuf *iobuf);
struct iobuf *iobuf_ref (struct iobuf *iobuf);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
void iobuf_to_iovec(struct iobuf *iob, struct iovec *iov);

#define iobuf_ptr(iob) ((iob)->ptr)
#define iobpool_default_pagesize(iobpool) ((iobpool)->default_page_size)
#define iobuf_pagesize(iob) (iob->iobuf_arena->page_size)


struct iobref {
        gf_lock_t          lock;
        int                ref;
        struct iobuf      *iobrefs[GF_IOBREF_IOBUF_COUNT];
};

struct iobref *iobref_new ();
struct iobref *iobref_ref (struct iobref *iobref);
void iobref_unref (struct iobref *iobref);
int iobref_add (struct iobref *iobref, struct iobuf *iobuf);
int iobref_merge (struct iobref *to, struct iobref *from);


size_t iobuf_size (struct iobuf *iobuf);
size_t iobref_size (struct iobref *iobref);
void   iobuf_stats_dump (struct iobuf_pool *iobuf_pool);

struct iobuf *
iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size);
#endif /* !_IOBUF_H_ */
