/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "dht-mem-types.h"
#include "libxlator.h"
#include "syncop.h"

#ifndef _DHT_H
#define _DHT_H

#define GF_XATTR_FIX_LAYOUT_KEY     "trusted.distribute.fix.layout"
#define GF_DHT_LOOKUP_UNHASHED_ON   1
#define GF_DHT_LOOKUP_UNHASHED_AUTO 2
#define DHT_PATHINFO_HEADER         "DISTRIBUTE:"

#include <fnmatch.h>

typedef int (*dht_selfheal_dir_cbk_t) (call_frame_t *frame, void *cookie,
                                       xlator_t     *this,
                                       int32_t       op_ret, int32_t op_errno);
typedef int (*dht_defrag_cbk_fn_t) (xlator_t        *this, call_frame_t *frame,
                                    int              ret);


struct dht_layout {
        int                spread_cnt;  /* layout spread count per directory,
                                           is controlled by 'setxattr()' with
                                           special key */
        int                cnt;
        int                preset;
        int                gen;
        int                type;
        int                ref; /* use with dht_conf_t->layout_lock */
        int                search_unhashed;
        struct {
                int        err;   /* 0 = normal
                                     -1 = dir exists and no xattr
                                     >0 = dir lookup failed with errno
                                  */
                uint32_t   start;
                uint32_t   stop;
                xlator_t  *xlator;
        } list[0];
};
typedef struct dht_layout  dht_layout_t;


typedef enum {
        DHT_HASH_TYPE_DM,
} dht_hashfn_type_t;

/* rebalance related */
struct dht_rebalance_ {
        xlator_t            *from_subvol;
        xlator_t            *target_node;
        int32_t              wbflags;
        off_t                offset;
        size_t               size;
        int32_t              flags;
        int                  count;
        struct iobref       *iobref;
        struct iovec        *vector;
        struct iatt          stbuf;
        dht_defrag_cbk_fn_t  target_op_fn;
};

struct dht_local {
        int                      call_cnt;
        loc_t                    loc;
        loc_t                    loc2;
        int                      op_ret;
        int                      op_errno;
        int                      layout_mismatch;
        /* Use stbuf as the postbuf, when we require both
         * pre and post attrs */
        struct iatt              stbuf;
        struct iatt              prebuf;
        struct iatt              preoldparent;
        struct iatt              postoldparent;
        struct iatt              preparent;
        struct iatt              postparent;
        struct statvfs           statvfs;
        fd_t                    *fd;
        inode_t                 *inode;
        dict_t                  *params;
        dict_t                  *xattr;
        dict_t                  *xattr_req;
        dht_layout_t            *layout;
        size_t                   size;
        ino_t                    ia_ino;
        xlator_t                *src_hashed, *src_cached;
        xlator_t                *dst_hashed, *dst_cached;
        xlator_t                *cached_subvol;
        xlator_t                *hashed_subvol;
        char                     need_selfheal;
        int                      file_count;
        int                      dir_count;
        call_frame_t            *main_frame;
        struct {
                fop_mknod_cbk_t  linkfile_cbk;
                struct iatt      stbuf;
                loc_t            loc;
                inode_t         *inode;
                dict_t          *xattr;
                xlator_t        *srcvol;
        } linkfile;
        struct {
                uint32_t         hole_cnt;
                uint32_t         overlaps_cnt;
                uint32_t         missing;
                uint32_t         down;
                uint32_t         misc;
                dht_selfheal_dir_cbk_t   dir_cbk;
                dht_layout_t    *layout;
        } selfheal;
        uint32_t                 uid;
        uint32_t                 gid;

        /* needed by nufa */
        int32_t flags;
        mode_t  mode;
        dev_t   rdev;

        /* need for file-info */
        char   *pathinfo;
        char   *key;

        char   *newpath;

        /* gfid related */
        uuid_t  gfid;

        /*Marker Related*/
        struct marker_str    marker;

        /* flag used to make sure we need to return estale in
           {lookup,revalidate}_cbk */
        char return_estale;
        char need_lookup_everywhere;

        glusterfs_fop_t      fop;

        struct dht_rebalance_ rebalance;
};
typedef struct dht_local dht_local_t;

/* du - disk-usage */
struct dht_du {
        double   avail_percent;
        uint64_t avail_space;
        uint32_t log;
};
typedef struct dht_du dht_du_t;

struct dht_conf {
        gf_lock_t      subvolume_lock;
        int            subvolume_cnt;
        xlator_t     **subvolumes;
        char          *subvolume_status;
        int           *last_event;
        dht_layout_t **file_layouts;
        dht_layout_t **dir_layouts;
        gf_boolean_t   search_unhashed;
        int            gen;
        dht_du_t      *du_stats;
        uint64_t       min_free_disk;
        char           disk_unit;
        int32_t        refresh_interval;
        gf_boolean_t   unhashed_sticky_bit;
        struct timeval last_stat_fetch;
        gf_lock_t      layout_lock;
        void          *private;     /* Can be used by wrapper xlators over
                                       dht */
        gf_boolean_t   use_readdirp;
        char           vol_uuid[UUID_SIZE + 1];
        gf_boolean_t   assert_no_child_down;
        time_t        *subvol_up_time;

        /* This is the count used as the distribute layout for a directory */
        /* Will be a global flag to control the layout spread count */
        uint32_t       dir_spread_cnt;

	struct syncenv *env; /* The env pointer to the rebalance synctask */

        /* to keep track of nodes which are decomissioned */
        xlator_t     **decommissioned_bricks;
};
typedef struct dht_conf dht_conf_t;


struct dht_disk_layout {
        uint32_t           cnt;
        uint32_t           type;
        struct {
                uint32_t   start;
                uint32_t   stop;
        } list[1];
};
typedef struct dht_disk_layout dht_disk_layout_t;

#define WIPE(statp) do { typeof(*statp) z = {0,}; if (statp) *statp = z; } while (0)

#define ENTRY_MISSING(op_ret, op_errno) (op_ret == -1 && op_errno == ENOENT)

#define is_fs_root(loc) (strcmp (loc->path, "/") == 0)

#define is_revalidate(loc) (inode_ctx_get (loc->inode, this, NULL) == 0)

#define is_last_call(cnt) (cnt == 0)

#define DHT_MIGRATION_IN_PROGRESS 1
#define DHT_MIGRATION_COMPLETED   2

#define DHT_LINKFILE_KEY         "trusted.glusterfs.dht.linkto"
#define DHT_LINKFILE_MODE        (S_ISVTX)

#define check_is_linkfile(i,s,x) (                                      \
                ((st_mode_from_ia ((s)->ia_prot, (s)->ia_type) & ~S_IFMT) \
                 == DHT_LINKFILE_MODE) &&                               \
                dict_get (x, DHT_LINKFILE_KEY))

#define check_is_linkfile_wo_dict(i,s) (                                \
                ((st_mode_from_ia ((s)->ia_prot, (s)->ia_type) & ~S_IFMT) \
                 == DHT_LINKFILE_MODE))

#define IS_DHT_MIGRATION_PHASE2(buf)  (                                 \
                IA_ISREG ((buf)->ia_type) &&                            \
                ((st_mode_from_ia ((buf)->ia_prot, (buf)->ia_type) &    \
                  ~S_IFMT) == DHT_LINKFILE_MODE))

#define IS_DHT_MIGRATION_PHASE1(buf)  (                                 \
                IA_ISREG ((buf)->ia_type) &&                            \
                ((buf)->ia_prot.sticky == 1) &&                         \
                ((buf)->ia_prot.sgid == 1))

#define DHT_STRIP_PHASE1_FLAGS(buf)  do {                       \
                if ((buf) && IS_DHT_MIGRATION_PHASE1(buf)) {    \
                        (buf)->ia_prot.sticky = 0;              \
                        (buf)->ia_prot.sgid = 0;                \
                }                                               \
        } while (0)

#define check_is_dir(i,s,x) (IA_ISDIR(s->ia_type))

#define layout_is_sane(layout) ((layout) && (layout->cnt > 0))

#define DHT_STACK_UNWIND(fop, frame, params ...) do {           \
                dht_local_t *__local = NULL;                    \
                xlator_t    *__xl    = NULL;                    \
                if (frame) {                                    \
                        __xl         = frame->this;             \
                        __local      = frame->local;            \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                dht_local_wipe (__xl, __local);                 \
        } while (0)

#define DHT_STACK_DESTROY(frame) do {                   \
                dht_local_t *__local = NULL;            \
                xlator_t    *__xl    = NULL;            \
                __xl                 = frame->this;     \
                __local              = frame->local;    \
                frame->local         = NULL;            \
                STACK_DESTROY (frame->root);            \
                dht_local_wipe (__xl, __local);         \
        } while (0)

dht_layout_t                            *dht_layout_new (xlator_t *this, int cnt);
dht_layout_t                            *dht_layout_get (xlator_t *this, inode_t *inode);
dht_layout_t                            *dht_layout_for_subvol (xlator_t *this, xlator_t *subvol);
xlator_t *dht_layout_search (xlator_t   *this, dht_layout_t *layout,
                             const char *name);
int                                      dht_layout_normalize (xlator_t *this, loc_t *loc, dht_layout_t *layout);
int dht_layout_anomalies (xlator_t      *this, loc_t *loc, dht_layout_t *layout,
                          uint32_t      *holes_p, uint32_t *overlaps_p,
                          uint32_t      *missing_p, uint32_t *down_p,
                          uint32_t      *misc_p);
int dht_layout_dir_mismatch (xlator_t   *this, dht_layout_t *layout,
                             xlator_t   *subvol, loc_t *loc, dict_t *xattr);

xlator_t *dht_linkfile_subvol (xlator_t *this, inode_t *inode,
                               struct iatt *buf, dict_t *xattr);
int dht_linkfile_unlink (call_frame_t      *frame, xlator_t *this,
                         xlator_t          *subvol, loc_t *loc);

int dht_layouts_init (xlator_t *this, dht_conf_t *conf);
int dht_layout_merge (xlator_t *this, dht_layout_t *layout, xlator_t *subvol,
                      int       op_ret, int op_errno, dict_t *xattr);

int dht_disk_layout_extract (xlator_t *this, dht_layout_t *layout,
                             int       pos, int32_t **disk_layout_p);
int dht_disk_layout_merge (xlator_t   *this, dht_layout_t *layout,
                           int         pos, void *disk_layout_raw);


int dht_frame_return (call_frame_t *frame);

int                             dht_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y);
int dht_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol,
                      uint64_t *x);

void dht_local_wipe (xlator_t *this, dht_local_t *local);
dht_local_t *dht_local_init (call_frame_t    *frame, loc_t *loc, fd_t *fd,
                             glusterfs_fop_t  fop);
int dht_iatt_merge (xlator_t                 *this, struct iatt *to, struct iatt *from,
                    xlator_t                 *subvol);

xlator_t *dht_subvol_get_hashed (xlator_t *this, loc_t *loc);
xlator_t *dht_subvol_get_cached (xlator_t *this, inode_t *inode);
xlator_t *dht_subvol_next (xlator_t *this, xlator_t *prev);
int       dht_subvol_cnt (xlator_t *this, xlator_t *subvol);

int dht_hash_compute (int type, const char *name, uint32_t *hash_p);

int dht_linkfile_create (call_frame_t    *frame, fop_mknod_cbk_t linkfile_cbk,
                         xlator_t        *tovol, xlator_t *fromvol, loc_t *loc);
int                                       dht_lookup_directory (call_frame_t *frame, xlator_t *this, loc_t *loc);
int                                       dht_lookup_everywhere (call_frame_t *frame, xlator_t *this, loc_t *loc);
int
dht_selfheal_directory (call_frame_t     *frame, dht_selfheal_dir_cbk_t cbk,
                        loc_t            *loc, dht_layout_t *layout);
int
dht_selfheal_new_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t cbk,
                            dht_layout_t *layout);
int
dht_selfheal_restore (call_frame_t       *frame, dht_selfheal_dir_cbk_t cbk,
                      loc_t              *loc, dht_layout_t *layout);
int
dht_layout_sort_volname (dht_layout_t *layout);

int dht_rename (call_frame_t *frame, xlator_t *this,
                loc_t *oldloc, loc_t *newloc);

int dht_get_du_info (call_frame_t *frame, xlator_t *this, loc_t *loc);

int       dht_is_subvol_filled (xlator_t *this, xlator_t *subvol);
xlator_t *dht_free_disk_available_subvol (xlator_t *this, xlator_t *subvol);
int       dht_get_du_info_for_subvol (xlator_t *this, int subvol_idx);

int dht_layout_preset (xlator_t *this, xlator_t *subvol, inode_t *inode);
int           dht_layout_set (xlator_t *this, inode_t *inode, dht_layout_t *layout);
void          dht_layout_unref (xlator_t *this, dht_layout_t *layout);
dht_layout_t *dht_layout_ref (xlator_t *this, dht_layout_t *layout);
xlator_t     *dht_first_up_subvol (xlator_t *this);
xlator_t     *dht_last_up_subvol (xlator_t *this);

int dht_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name);

int dht_filter_loc_subvol_key (xlator_t  *this, loc_t *loc, loc_t *new_loc,
                               xlator_t **subvol);

int                                     dht_rename_cleanup (call_frame_t *frame);
int dht_rename_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t           op_ret, int32_t op_errno,
                          inode_t          *inode, struct iatt *stbuf,
                          struct iatt      *preparent, struct iatt *postparent);

int dht_fix_directory_layout (call_frame_t *frame,
                              dht_selfheal_dir_cbk_t  dir_cbk,
                              dht_layout_t           *layout);

int dht_init_subvolumes (xlator_t *this, dht_conf_t *conf);

/* migration/rebalance */
int dht_start_rebalance_task (xlator_t *this, call_frame_t *frame);

int dht_rebalance_in_progress_check (xlator_t *this, call_frame_t *frame);
int dht_rebalance_complete_check (xlator_t *this, call_frame_t *frame);


/* FOPS */
int32_t dht_lookup (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc,
                    dict_t   *xattr_req);

int32_t dht_stat (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *loc);

int32_t dht_fstat (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd);

int32_t dht_truncate (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      off_t     offset);

int32_t dht_ftruncate (call_frame_t *frame,
                       xlator_t *this,
                       fd_t     *fd,
                       off_t     offset);

int32_t dht_access (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc,
                    int32_t   mask);

int32_t dht_readlink (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      size_t    size);

int32_t dht_mknod (call_frame_t *frame, xlator_t *this,
                   loc_t    *loc, mode_t mode, dev_t rdev, dict_t *params);

int32_t dht_mkdir (call_frame_t *frame, xlator_t *this,
                   loc_t    *loc, mode_t mode, dict_t *params);

int32_t dht_unlink (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc);

int32_t dht_rmdir (call_frame_t *frame, xlator_t *this,
                   loc_t    *loc, int flags);

int32_t dht_symlink (call_frame_t   *frame, xlator_t *this,
                     const char *linkpath, loc_t *loc, dict_t *params);

int32_t dht_rename (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *oldloc,
                    loc_t    *newloc);

int32_t dht_link (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *oldloc,
                  loc_t    *newloc);

int32_t dht_create (call_frame_t *frame, xlator_t *this,
                    loc_t    *loc, int32_t flags, mode_t mode,
                    fd_t     *fd, dict_t *params);

int32_t dht_open (call_frame_t *frame,
                  xlator_t *this,
                  loc_t    *loc,
                  int32_t   flags, fd_t *fd,
                  int32_t   wbflags);

int32_t dht_readv (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd,
                   size_t    size,
                   off_t     offset);

int32_t dht_writev (call_frame_t      *frame,
                    xlator_t      *this,
                    fd_t          *fd,
                    struct iovec  *vector,
                    int32_t        count,
                    off_t          offset,
                    struct iobref *iobref);

int32_t dht_flush (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd);

int32_t dht_fsync (call_frame_t *frame,
                   xlator_t *this,
                   fd_t     *fd,
                   int32_t   datasync);

int32_t dht_opendir (call_frame_t *frame,
                     xlator_t *this,
                     loc_t    *loc, fd_t *fd);

int32_t dht_fsyncdir (call_frame_t *frame,
                      xlator_t *this,
                      fd_t     *fd,
                      int32_t   datasync);

int32_t dht_statfs (call_frame_t *frame,
                    xlator_t *this,
                    loc_t    *loc);

int32_t dht_setxattr (call_frame_t *frame,
                      xlator_t *this,
                      loc_t    *loc,
                      dict_t   *dict,
                      int32_t   flags);

int32_t dht_getxattr (call_frame_t   *frame,
                      xlator_t   *this,
                      loc_t      *loc,
                      const char *name);

int32_t dht_fsetxattr (call_frame_t *frame,
                       xlator_t *this,
                       fd_t     *fd,
                       dict_t   *dict,
                       int32_t   flags);

int32_t dht_fgetxattr (call_frame_t   *frame,
                       xlator_t   *this,
                       fd_t       *fd,
                       const char *name);

int32_t dht_removexattr (call_frame_t   *frame,
                         xlator_t   *this,
                         loc_t      *loc,
                         const char *name);

int32_t dht_lk (call_frame_t        *frame,
                xlator_t        *this,
                fd_t            *fd,
                int32_t          cmd,
                struct gf_flock *flock);

int32_t dht_inodelk (call_frame_t *frame, xlator_t *this,
                     const char      *volume, loc_t *loc, int32_t cmd,
                     struct gf_flock *flock);

int32_t dht_finodelk (call_frame_t        *frame, xlator_t *this,
                      const char      *volume, fd_t *fd, int32_t cmd,
                      struct gf_flock *flock);

int32_t dht_entrylk (call_frame_t    *frame, xlator_t *this,
                     const char  *volume, loc_t *loc, const char *basename,
                     entrylk_cmd  cmd, entrylk_type type);

int32_t dht_fentrylk (call_frame_t    *frame, xlator_t *this,
                      const char  *volume, fd_t *fd, const char *basename,
                      entrylk_cmd  cmd, entrylk_type type);

int32_t dht_readdir (call_frame_t  *frame,
                     xlator_t *this,
                     fd_t     *fd,
                     size_t    size, off_t off);

int32_t dht_readdirp (call_frame_t *frame,
                      xlator_t *this,
                      fd_t     *fd,
                      size_t    size, off_t off);

int32_t dht_xattrop (call_frame_t           *frame,
                     xlator_t           *this,
                     loc_t              *loc,
                     gf_xattrop_flags_t  flags,
                     dict_t             *dict);

int32_t dht_fxattrop (call_frame_t *frame,
                      xlator_t           *this,
                      fd_t               *fd,
                      gf_xattrop_flags_t  flags,
                      dict_t             *dict);

int32_t dht_forget (xlator_t *this, inode_t *inode);
int32_t dht_setattr (call_frame_t  *frame, xlator_t *this, loc_t *loc,
                     struct iatt   *stbuf, int32_t valid);
int32_t dht_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      struct iatt  *stbuf, int32_t valid);

int32_t dht_notify (xlator_t *this, int32_t event, void *data, ...);

/* definitions for nufa/switch */
int dht_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent);
int dht_lookup_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent);
int dht_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int op_ret, int op_errno,
                             inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                             struct iatt *postparent);
int dht_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                    struct iatt *postparent);
int dht_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    fd_t *fd, inode_t *inode, struct iatt *stbuf,
                    struct iatt *preparent, struct iatt *postparent);
int dht_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno,
                     inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                     struct iatt *postparent);



#endif/* _DHT_H */
