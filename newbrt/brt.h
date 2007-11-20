#ifndef BRT_H
#define BRT_H

// This must be first to make the 64-bit file mode work right in Linux
#define _FILE_OFFSET_BITS 64
#include "brttypes.h"

#include "ybt.h"
#include "../include/db.h"
#include "cachetable.h"
#include "log.h"
typedef struct brt *BRT;
int open_brt (const char *fname, const char *dbname, int is_create, BRT *, int nodesize, CACHETABLE, TOKUTXN, int(*)(DB*,const DBT*,const DBT*));

int brt_create(BRT *);
int brt_set_flags(BRT, int flags);

enum {
    TOKU_DB_DUP = 1,
    TOKU_DB_DUPSORT = 2,
};
    
int brt_set_nodesize(BRT, int nodesize);
int brt_set_bt_compare(BRT, int (*bt_compare)(DB *, const DBT*, const DBT*));
int brt_set_dup_compare(BRT, int (*dup_compare)(DB *, const DBT*, const DBT*));
int brt_set_cachetable(BRT, CACHETABLE);
int brt_open(BRT, const char *fname, const char *dbname, int is_create, int only_create, CACHETABLE ct, TOKUTXN txn);
int brt_remove_subdb(BRT brt, const char *dbname, u_int32_t flags);

int brt_insert (BRT, DBT *, DBT *, DB*, TOKUTXN);
int brt_lookup (BRT brt, DBT *k, DBT *v, DB*db);
int brt_delete (BRT brt, DBT *k, DB *db);
int close_brt (BRT);
int dump_brt (BRT brt);
void brt_fsync (BRT); /* fsync, but don't clear the caches. */

void brt_flush (BRT); /* fsync and clear the caches. */ 

/* create and initialize a cache table
   cachesize is the upper limit on the size of the size of the values in the table 
   pass 0 if you want the default */

int brt_create_cachetable(CACHETABLE *t, long cachesize, LSN initial_lsn, TOKULOGGER);

extern int brt_debug_mode;
int toku_verify_brt (BRT brt);

int show_brt_blocknumbers(BRT);

typedef struct brt_cursor *BRT_CURSOR;
int brt_cursor (BRT, BRT_CURSOR*);
int brt_cursor_get (BRT_CURSOR cursor, DBT *kbt, DBT *vbt, int brtc_flags, DB *db, TOKUTXN);
int brt_cursor_delete(BRT_CURSOR cursor, int flags);
int brt_cursor_close (BRT_CURSOR curs);

typedef struct brtenv *BRTENV;
int brtenv_checkpoint (BRTENV env);

#endif
