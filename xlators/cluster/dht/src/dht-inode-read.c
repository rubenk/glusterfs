/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.h"

int dht_access2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_readv2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_attr2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_open2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_flush2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_lk2 (xlator_t *this, call_frame_t *frame, int ret);
int dht_fsync2 (xlator_t *this, call_frame_t *frame, int ret);

int
dht_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno, fd_t *fd)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = 0;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if ((op_ret == -1) && (op_errno != ENOENT)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "subvolume %s returned -1 (%s)",
                        prev->this->name, strerror (op_errno));
                goto out;
        }

        if (!op_ret || (local->call_cnt != 1))
                goto out;

        /* rebalance would have happened */
        local->rebalance.target_op_fn = dht_open2;
        ret = dht_rebalance_complete_check (this, frame);
        if (!ret)
                return 0;

out:
        DHT_STACK_UNWIND (open, frame, op_ret, op_errno, local->fd);

        return 0;
}

int
dht_open2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = ENOENT;
        if (op_ret)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_open_cbk, subvol, subvol->fops->open,
                    &local->loc, local->rebalance.flags, local->fd,
                    local->rebalance.wbflags);
        return 0;

out:
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL);
        return 0;
}

int
dht_open (call_frame_t *frame, xlator_t *this,
          loc_t *loc, int flags, fd_t *fd, int wbflags)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, loc, fd, GF_FOP_OPEN);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->rebalance.wbflags = wbflags;
        local->rebalance.flags = flags;
        local->call_cnt = 1;

        STACK_WIND (frame, dht_open_cbk, subvol, subvol->fops->open,
                    loc, flags, fd, wbflags);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (open, frame, -1, op_errno, NULL);

        return 0;
}

int
dht_file_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *stbuf)
{
        uint64_t      tmp_subvol = 0;
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        if ((op_ret == -1) && (op_errno != ENOENT)) {
                local->op_errno = op_errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "subvolume %s returned -1 (%s)",
                        prev->this->name, strerror (op_errno));
                goto out;
        }

        if (local->call_cnt != 1)
                goto out;

        /* Check if the rebalance phase2 is true */
        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (stbuf)) {
                if (local->fd)
                        ret = fd_ctx_get (local->fd, this, &tmp_subvol);
                if (ret) {
                        /* Phase 2 of migration */
                        local->rebalance.target_op_fn = dht_attr2;
                        ret = dht_rebalance_complete_check (this, frame);
                } else {
                        /* value is already set in fd_ctx, that means no need
                           to check for whether its complete or not. */
                        dht_attr2 (this, frame, 0);
                }
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (stat, frame, op_ret, op_errno, stbuf);
err:
        return 0;
}

int
dht_attr2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        subvol = local->cached_subvol;
        local->call_cnt = 2;

        if (local->fop == GF_FOP_FSTAT) {
                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->fstat, local->fd);
        } else {
                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->stat, &local->loc);
        }
        return 0;

out:
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL);
        return 0;
}

int
dht_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno, struct iatt *stbuf)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));

                        goto unlock;
                }

                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);
out:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (stat, frame, local->op_ret, local->op_errno,
                                  &local->stbuf);
        }
err:
        return 0;
}

int
dht_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           i = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);


        local = dht_local_init (frame, loc, NULL, GF_FOP_STAT);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (loc->inode->ia_type)) {
                local->call_cnt = 1;

                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->stat, loc);

                return 0;
        }

        local->call_cnt = layout->cnt;

        for (i = 0; i < layout->cnt; i++) {
                subvol = layout->list[i].xlator;

                STACK_WIND (frame, dht_attr_cbk,
                            subvol, subvol->fops->stat,
                            loc);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (stat, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           i = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSTAT);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        if (IA_ISREG (fd->inode->ia_type)) {
                local->call_cnt = 1;

                subvol = local->cached_subvol;

                STACK_WIND (frame, dht_file_attr_cbk, subvol,
                            subvol->fops->fstat, fd);

                return 0;
        }

        local->call_cnt = layout->cnt;

        for (i = 0; i < layout->cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND (frame, dht_attr_cbk,
                            subvol, subvol->fops->fstat,
                            fd);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fstat, frame, -1, op_errno, NULL);

        return 0;
}

int
dht_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               struct iovec *vector, int count, struct iatt *stbuf,
               struct iobref *iobref)
{
        dht_local_t *local      = NULL;
        int          ret        = 0;

        local = frame->local;
        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        /* This is already second try, no need for re-check */
        if (local->call_cnt != 1)
                goto out;

        if ((op_ret == -1) && (op_errno != ENOENT))
                goto out;

        if ((op_ret == -1) || IS_DHT_MIGRATION_PHASE2 (stbuf)) {
                /* File would be migrated to other node */
                ret = fd_ctx_get (local->fd, this, NULL);
                if (ret) {
                        local->rebalance.target_op_fn = dht_readv2;
                        ret = dht_rebalance_complete_check (this, frame);
                } else {
                        /* value is already set in fd_ctx, that means no need
                           to check for whether its complete or not. */
                        dht_readv2 (this, frame, 0);
                }
                if (!ret)
                        return 0;
        }

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (readv, frame, op_ret, op_errno, vector, count, stbuf,
                          iobref);

        return 0;
}

int
dht_readv2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_readv_cbk, subvol, subvol->fops->readv,
                    local->fd, local->rebalance.size, local->rebalance.offset);

        return 0;

out:
        DHT_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);
        return 0;
}

int
dht_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t off)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_READ);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->rebalance.offset = off;
        local->rebalance.size   = size;
        local->call_cnt = 1;

        STACK_WIND (frame, dht_readv_cbk,
                    subvol, subvol->fops->readv,
                    fd, size, off);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL);

        return 0;
}

int
dht_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno)
{
        int          ret = -1;
        dht_local_t *local = NULL;

        local = frame->local;

        if (local->call_cnt != 1)
                goto out;

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                /* File would be migrated to other node */
                local->rebalance.target_op_fn = dht_access2;
                ret = dht_rebalance_complete_check (frame->this, frame);
                if (!ret)
                        return 0;
        }

out:
        DHT_STACK_UNWIND (access, frame, op_ret, op_errno);
        return 0;
}

int
dht_access2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t *local  = NULL;
        xlator_t    *subvol = NULL;
        int          op_errno = EINVAL;

        local = frame->local;
        if (!local)
                goto out;

        op_errno = local->op_errno;
        if (op_ret == -1)
                goto out;

        local->call_cnt = 2;
        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_access_cbk, subvol, subvol->fops->access,
                    &local->loc, local->rebalance.flags);

        return 0;

out:
        DHT_STACK_UNWIND (access, frame, -1, op_errno);
        return 0;
}


int
dht_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_ACCESS);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->rebalance.flags = mask;
        local->call_cnt = 1;
        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_access_cbk, subvol, subvol->fops->access,
                    loc, mask);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (access, frame, -1, op_errno);

        return 0;
}


int
dht_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno)
{
        dht_local_t  *local = NULL;
        int           ret = -1;

        local = frame->local;

        local->op_errno = op_errno;

        if (local->call_cnt != 1)
                goto out;

        /* If context is set, then send flush() it to the destination */
        ret = fd_ctx_get (local->fd, this, NULL);
        if (!ret) {
                dht_flush2 (this, frame, 0);
                return 0;
        }

out:
        DHT_STACK_UNWIND (flush, frame, op_ret, op_errno);

        return 0;
}

int
dht_flush2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;
        uint64_t      tmp_subvol = 0;
        int           ret = -1;

        local = frame->local;

        ret = fd_ctx_get (local->fd, this, &tmp_subvol);
        if (!ret)
                subvol = (xlator_t *)(long)tmp_subvol;

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_flush_cbk,
                    subvol, subvol->fops->flush, local->fd);

        return 0;
}


int
dht_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FLUSH);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame, dht_flush_cbk,
                    subvol, subvol->fops->flush, fd);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (flush, frame, -1, op_errno);

        return 0;
}


int
dht_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct iatt *prebuf, struct iatt *postbuf)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           ret = -1;

        local = frame->local;
        prev = cookie;

        local->op_errno = op_errno;
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "subvolume %s returned -1 (%s)",
                        prev->this->name, strerror (op_errno));
                goto out;
        }

        if (local->call_cnt != 1) {
                if (local->stbuf.ia_blocks) {
                        dht_iatt_merge (this, postbuf, &local->stbuf, NULL);
                        dht_iatt_merge (this, prebuf, &local->prebuf, NULL);
                }
                goto out;
        }

        ret = fd_ctx_get (local->fd, this, NULL);
        if (ret) {
                local->rebalance.target_op_fn = dht_fsync2;

                /* Check if the rebalance phase1 is true */
                if (IS_DHT_MIGRATION_PHASE1 (postbuf)) {
                        dht_iatt_merge (this, &local->stbuf, postbuf, NULL);
                        dht_iatt_merge (this, &local->prebuf, prebuf, NULL);

                        ret = dht_rebalance_in_progress_check (this, frame);
                }

                /* Check if the rebalance phase2 is true */
                if (IS_DHT_MIGRATION_PHASE2 (postbuf)) {
                        ret = dht_rebalance_complete_check (this, frame);
                }
        } else {
                dht_fsync2 (this, frame, 0);
        }
        if (!ret)
                return 0;

out:
        DHT_STRIP_PHASE1_FLAGS (postbuf);
        DHT_STRIP_PHASE1_FLAGS (prebuf);
        DHT_STACK_UNWIND (fsync, frame, op_ret, op_errno,
                          prebuf, postbuf);

        return 0;
}

int
dht_fsync2 (xlator_t *this, call_frame_t *frame, int op_ret)
{
        dht_local_t  *local  = NULL;
        xlator_t     *subvol = NULL;
        uint64_t      tmp_subvol = 0;
        int           ret = -1;

        local = frame->local;

        ret = fd_ctx_get (local->fd, this, &tmp_subvol);
        if (!ret)
                subvol = (xlator_t *)(long)tmp_subvol;

        if (!subvol)
                subvol = local->cached_subvol;

        local->call_cnt = 2; /* This is the second attempt */

        STACK_WIND (frame, dht_fsync_cbk, subvol, subvol->fops->fsync,
                    local->fd, local->rebalance.flags);

        return 0;
}

int
dht_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSYNC);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        local->call_cnt = 1;
        local->rebalance.flags = datasync;

        subvol = local->cached_subvol;

        STACK_WIND (frame, dht_fsync_cbk, subvol, subvol->fops->fsync,
                    fd, datasync);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL);

        return 0;
}


/* TODO: for 'lk()' call, we need some other special error, may be ESTALE to
   indicate that lock migration happened on the fd, so we can consider it as
   phase 2 of migration */
int
dht_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int op_ret, int op_errno, struct gf_flock *flock)
{
        DHT_STACK_UNWIND (lk, frame, op_ret, op_errno, flock);

        return 0;
}


int
dht_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int cmd, struct gf_flock *flock)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        /* TODO: for rebalance, we need to preserve the fop arguments */
        STACK_WIND (frame, dht_lk_cbk, subvol, subvol->fops->lk, fd,
                    cmd, flock);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lk, frame, -1, op_errno, NULL);

        return 0;
}

/* Symlinks are currently not migrated, so no need for any check here */
int
dht_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, const char *path, struct iatt *stbuf)
{
        dht_local_t *local = NULL;

        local = frame->local;
        if (op_ret == -1)
                goto err;

        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
        }

err:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, stbuf);

        return 0;
}


int
dht_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_READLINK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_readlink_cbk,
                    subvol, subvol->fops->readlink,
                    loc, size);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL);

        return 0;
}

/* Currently no translators on top of 'distribute' will be using
 * below fops, hence not implementing 'migration' related checks
 */

int
dht_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        DHT_STACK_UNWIND (xattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
dht_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t flags, dict_t *dict)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_XATTROP);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame,
                    dht_xattrop_cbk,
                    subvol, subvol->fops->xattrop,
                    loc, flags, dict);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        DHT_STACK_UNWIND (fxattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
dht_fxattrop (call_frame_t *frame, xlator_t *this,
              fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame,
                    dht_fxattrop_cbk,
                    subvol, subvol->fops->fxattrop,
                    fd, flags, dict);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_inodelk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        DHT_STACK_UNWIND (inodelk, frame, op_ret, op_errno);
        return 0;
}


int32_t
dht_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *lock)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_INODELK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame,
                    dht_inodelk_cbk,
                    subvol, subvol->fops->inodelk,
                    volume, loc, cmd, lock);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (inodelk, frame, -1, op_errno);

        return 0;
}


int
dht_finodelk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        DHT_STACK_UNWIND (finodelk, frame, op_ret, op_errno);
        return 0;
}


int
dht_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }


        STACK_WIND (frame,
                    dht_finodelk_cbk,
                    subvol, subvol->fops->finodelk,
                    volume, fd, cmd, lock);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (finodelk, frame, -1, op_errno);

        return 0;
}
