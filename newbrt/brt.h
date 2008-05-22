/* -*- mode: C; c-basic-offset: 4 -*- */
#ifndef BRT_H
#define BRT_H
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

// This must be first to make the 64-bit file mode work right in Linux
#define _FILE_OFFSET_BITS 64
#include "brttypes.h"
#include "ybt.h"
#include "../include/db.h"
#include "cachetable.h"
#include "log.h"
#include "brt-search.h"

int toku_open_brt (const char *fname, const char *dbname, int is_create, BRT *, int nodesize, CACHETABLE, TOKUTXN, int(*)(DB*,const DBT*,const DBT*), DB*);

int toku_brt_create(BRT *);
int toku_brt_set_flags(BRT, unsigned int flags);
int toku_brt_get_flags(BRT, unsigned int *flags);
int toku_brt_set_nodesize(BRT, unsigned int nodesize);
int toku_brt_get_nodesize(BRT, unsigned int *nodesize);
int toku_brt_set_bt_compare(BRT, int (*bt_compare)(DB *, const DBT*, const DBT*));
int toku_brt_set_dup_compare(BRT, int (*dup_compare)(DB *, const DBT*, const DBT*));
int brt_set_cachetable(BRT, CACHETABLE);
int toku_brt_open(BRT, const char *fname, const char *fname_in_env, const char *dbname, int is_create, int only_create, CACHETABLE ct, TOKUTXN txn, DB *db);
int toku_brt_remove_subdb(BRT brt, const char *dbname, u_int32_t flags);

int toku_brt_insert (BRT, DBT *, DBT *, TOKUTXN);
int toku_brt_lookup (BRT brt, DBT *k, DBT *v);
int toku_brt_delete (BRT brt, DBT *k, TOKUTXN);
int toku_brt_delete_both (BRT brt, DBT *k, DBT *v, TOKUTXN); // Delete a pair only if both k and v are equal according to the comparison function.
int toku_close_brt (BRT, TOKULOGGER);
int toku_dump_brt (BRT brt);
void brt_fsync (BRT); /* fsync, but don't clear the caches. */

void brt_flush (BRT); /* fsync and clear the caches. */ 

/* create and initialize a cache table
   cachesize is the upper limit on the size of the size of the values in the table 
   pass 0 if you want the default */

int toku_brt_create_cachetable(CACHETABLE *t, long cachesize, LSN initial_lsn, TOKULOGGER);

extern int toku_brt_debug_mode;
int toku_verify_brt (BRT brt);

//int show_brt_blocknumbers(BRT);

typedef struct brt_cursor *BRT_CURSOR;
int toku_brt_cursor (BRT, BRT_CURSOR*, int is_temporary_cursor);
int toku_brt_cursor_get (BRT_CURSOR cursor, DBT *kbt, DBT *vbt, int brtc_flags, TOKUTXN);
int toku_brt_cursor_delete(BRT_CURSOR cursor, int flags, TOKUTXN);
int toku_brt_cursor_close (BRT_CURSOR curs);
BOOL toku_brt_cursor_uninitialized(BRT_CURSOR c);

typedef struct brtenv *BRTENV;
int brtenv_checkpoint (BRTENV env);

extern int toku_brt_do_push_cmd; // control whether push occurs eagerly.


int toku_brt_dbt_set(DBT* key, DBT* key_source);
int toku_brt_cursor_copyout(BRT_CURSOR cursor, DBT *key, DBT *val);
int toku_brt_cursor_copyout_with_dat(BRT_CURSOR cursor, DBT *key, DBT *val,
                                     BRT pdb, DBT* dat, DBT* dat_source);

int toku_brt_get_fd(BRT, int *);

int toku_brt_height_of_root(BRT, int *height); // for an open brt, return the current height.

enum brt_header_flags {
    TOKU_DB_DUP = 1,
    TOKU_DB_DUPSORT = 2,
};

int toku_brt_keyrange (BRT brt, DBT *key, u_int64_t *less,  u_int64_t *equal,  u_int64_t *greater);

#endif
