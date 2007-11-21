#include "cachetable.h"
#include "hashtable.h"
#include "pma.h"
#include "brt.h"
#include "crc.h"

#ifndef BRT_FANOUT
#define BRT_FANOUT 16
#endif
enum { TREE_FANOUT = BRT_FANOUT };
enum { KEY_VALUE_OVERHEAD = 8 }; /* Must store the two lengths. */
enum { BRT_CMD_OVERHEAD = 1 };
enum { BRT_DEFAULT_NODE_SIZE = 1 << 20 };

struct nodeheader_in_file {
    int n_in_buffer;
};
enum { BUFFER_HEADER_SIZE = (4 // height//
			     + 4 // n_children
			     + TREE_FANOUT * 8 // children
			     ) };
typedef struct brtnode *BRTNODE;
/* Internal nodes. */
struct brtnode {
    enum typ_tag tag;
    //    BRT          brt;       // The containing BRT     
    unsigned int nodesize;
    DISKOFF thisnodename;   // The size of the node allocated on disk.  Not all is necessarily in use.
    LSN     lsn;            // Need the LSN as of the most recent modification.
    int     layout_version; // What version of the data structure?
    BRTNODE parent_brtnode; /* Invariant: The parent of an in-memory node must be in main memory.  This is so we can find and update the down pointer when we change the diskoff of a node. */
    int    height; /* height is always >= 0.  0 for leaf, >0 for nonleaf. */
    u_int32_t rand4fingerprint;
    u_int32_t local_fingerprint; /* For leaves this is everything in the buffer.  For nonleaves, this is everything in the hash tables, but does not include child subtree fingerprints. */
    int    dirty;
    union node {
	struct nonleaf {
	    // Don't actually store the subree fingerprint in the in-memory data structure.
	    int             n_children;  /* if n_children==TREE_FANOUT+1 then the tree needs to be rebalanced. */
	    u_int32_t       child_subtree_fingerprints[TREE_FANOUT+1];
	    bytevec         childkeys[TREE_FANOUT];   /* Pivot keys.  Child 0's keys are <= childkeys[0].  Child 1's keys are <= childkeys[1].
							 Note: It is possible that Child 1's keys are == to child 0's key's, so it is
							 not necessarily true that child 1's keys are > childkeys[0].
						         However, in the absense of duplicate keys, child 1's keys *are* > childkeys[0]. */
	    unsigned int    childkeylens[TREE_FANOUT];
	    unsigned int    totalchildkeylens;
            unsigned char   pivotflags[TREE_FANOUT];
	    DISKOFF         children[TREE_FANOUT+1];  /* unused if height==0 */   /* Note: The last element of these arrays is used only temporarily while splitting a node. */
	    HASHTABLE       htables[TREE_FANOUT+1];
	    unsigned int    n_bytes_in_hashtable[TREE_FANOUT+1]; /* how many bytes are in each hashtable (including overheads) */
	    unsigned int    n_bytes_in_hashtables;
            unsigned int    n_cursors[TREE_FANOUT+1];
        } n;
	struct leaf {
	    PMA buffer;
	    unsigned int n_bytes_in_buffer;
	} l;
    } u;
};

/* pivot flags  (must fit in 8 bits) */
enum {
    BRT_PIVOT_PRESENT_L = 1,
    BRT_PIVOT_PRESENT_R = 2,
    BRT_PIVOT_TRUNC = 4,
    BRT_PIVOT_FRONT_COMPRESS = 8,
};

struct brt_header {
    int dirty;
    unsigned int nodesize;
    DISKOFF freelist;
    DISKOFF unused_memory;
    DISKOFF unnamed_root;
    int n_named_roots; /* -1 if the only one is unnamed */
    char  **names;
    DISKOFF *roots;
    unsigned int flags;
};


struct brt {
    CACHEFILE cf;
    char *database_name;
    // The header is shared.  It is also ephemeral.
    struct brt_header *h;

    BRT_CURSOR cursors_head, cursors_tail;

    unsigned int nodesize;
    unsigned int flags;
    int (*compare_fun)(DB*,const DBT*,const DBT*);
    int (*dup_compare)(DB*,const DBT*,const DBT*);

    void *skey,*sval; /* Used for DBT return values. */
};

/* serialization code */
void toku_serialize_brtnode_to(int fd, DISKOFF off, DISKOFF size, BRTNODE node);
int toku_deserialize_brtnode_from (int fd, DISKOFF off, BRTNODE *brtnode, int flags, int nodesize, int (*bt_compare)(DB *, const DBT*, const DBT*), int (*dup_compare)(DB *, const DBT *, const DBT *));
unsigned int toku_serialize_brtnode_size(BRTNODE node); /* How much space will it take? */
int toku_keycompare (bytevec key1, ITEMLEN key1len, bytevec key2, ITEMLEN key2len);

void toku_verify_counts(BRTNODE);

int toku_serialize_brt_header_size (struct brt_header *h);
int toku_serialize_brt_header_to (int fd, struct brt_header *h);
int toku_serialize_brt_header_to_wbuf (struct wbuf *, struct brt_header *h);
int toku_deserialize_brtheader_from (int fd, DISKOFF off, struct brt_header **brth);

/* return the size of a tree node */
long brtnode_size (BRTNODE node);

void brtnode_free (BRTNODE *node);

//static inline int brtnode_n_hashtables(BRTNODE node) { if (node->height==0) return 1; else return node->u.n.n_children; }

//int write_brt_header (int fd, struct brt_header *header);

#if 1
#define DEADBEEF ((void*)0xDEADBEEF)
#else
#define DEADBEEF ((void*)0xDEADBEEFDEADBEEF)
#endif


#define CURSOR_PATHLEN_LIMIT 256
struct brt_cursor {
    BRT brt;
    int path_len;  /* -1 if the cursor points nowhere. */
    BRTNODE path[CURSOR_PATHLEN_LIMIT]; /* Include the leaf (last).    These are all pinned. */
    int pathcnum[CURSOR_PATHLEN_LIMIT]; /* which child did we descend to from here? */
    PMA_CURSOR pmacurs; /* The cursor into the leaf.  NULL if the cursor doesn't exist. */
    BRT_CURSOR prev,next;
    int op;
};

/* print the cursor path */
void brt_cursor_print(BRT_CURSOR cursor);

/* is the cursor path empty? */
static inline int brt_cursor_path_empty(BRT_CURSOR cursor) {
    return cursor->path_len == 0;
}

/*is the cursor path full? */
static inline int brt_cursor_path_full(BRT_CURSOR cursor) {
    return cursor->path_len == CURSOR_PATHLEN_LIMIT;
}

static inline int brt_cursor_active(BRT_CURSOR cursor) {
    return cursor->path_len > 0;
}

/* brt has a new root.  add the root to this cursor. */
void brt_cursor_new_root(BRT_CURSOR cursor, BRT t, BRTNODE newroot, BRTNODE left, BRTNODE right);

/* a brt leaf has split.  modify this cursor if it includes the old node in its path. */
void brt_cursor_leaf_split(BRT_CURSOR cursor, BRT t, BRTNODE oldnode, BRTNODE left, BRTNODE right);

/* a brt internal node has expanded.  modify this cursor if it includes the  old node in its path. */
void brt_cursor_nonleaf_expand(BRT_CURSOR cursor, BRT t, BRTNODE oldnode, int childnum, BRTNODE left, BRTNODE right);

/* a brt internal node has split.  modify this cursor if it includes the old node in its path. */
void brt_cursor_nonleaf_split(BRT_CURSOR cursor, BRT t, BRTNODE oldnode, BRTNODE left, BRTNODE right);

void brt_update_cursors_new_root(BRT t, BRTNODE newroot, BRTNODE left, BRTNODE right);
void brt_update_cursors_leaf_split(BRT t, BRTNODE oldnode, BRTNODE left, BRTNODE right);
void brt_update_cursors_nonleaf_expand(BRT t, BRTNODE oldnode, int childnum, BRTNODE left, BRTNODE right);
void brt_update_cursors_nonleaf_split(BRT t, BRTNODE oldnode, BRTNODE left, BRTNODE right);

enum brt_cmd_type {
    BRT_NONE = 0,
    BRT_INSERT = 1,
    BRT_DELETE = 2,
};

struct brt_cmd {
    enum brt_cmd_type type;
    union {
        /* insert or delete */
        struct brt_cmd_insert_delete {
            DBT *key;
            DBT *val;
            DB *db;
        } id;
    } u;
};
typedef struct brt_cmd BRT_CMD;

struct brtenv {
    CACHETABLE ct;
    TOKULOGGER logger;
    long long checksum_number;
//    SPINLOCK  checkpointing;
};

extern cachetable_flush_func_t brtnode_flush_callback;
extern cachetable_fetch_func_t brtnode_fetch_callback;
extern int toku_read_and_pin_brt_header (CACHEFILE cf, struct brt_header **header);
extern int toku_unpin_brt_header (BRT brt);
extern CACHEKEY* toku_calculate_root_offset_pointer (BRT brt);

static const BRTNODE null_brtnode=0;

extern u_int32_t toku_calccrc32_kvpair (const void *key, int keylen, const void *val, int vallen);
extern u_int32_t toku_calccrc32_cmd (int type, const void *key, int keylen, const void *val, int vallen);
extern u_int32_t toku_calccrc32_cmdstruct (BRT_CMD *cmd);
