/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

/* rollback and rollforward routines. */

#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>

#include "log_header.h"
#include "log-internal.h"
#include "cachetable.h"
#include "key.h"

//#define DO_VERIFY_COUNTS
#ifdef DO_VERIFY_COUNTS
#define VERIFY_COUNTS(n) toku_verify_counts(n)
#else
#define VERIFY_COUNTS(n) ((void)0)
#endif

#define UU(x) x __attribute__((__unused__))

static DB * const null_db=0;

// These data structures really should be part of a recovery data structure.  Recovery could be multithreaded (on different environments...)  But this is OK since recovery can only happen in one
static CACHETABLE ct;
static struct cf_pair {
    FILENUM filenum;
    CACHEFILE cf;
    BRT       brt; // set to zero on an fopen, but filled in when an fheader is seen.
} *cf_pairs;
static int n_cf_pairs=0, max_cf_pairs=0;;

int toku_recover_init (void) {
    int r = toku_create_cachetable(&ct, 1<<25, (LSN){0}, 0);
    return r;
}
void toku_recover_cleanup (void) {
    int i;
    for (i=0; i<n_cf_pairs; i++) {
	if (cf_pairs[i].brt) {
	    int r = toku_close_brt(cf_pairs[i].brt);
	    //r = toku_cachefile_close(&cf_pairs[i].cf);
	    assert(r==0);
	}
    }
    toku_free(cf_pairs);
    {
	int r = toku_cachetable_close(&ct);
	assert(r==0);
    }
}

int toku_recover_note_cachefile (FILENUM fnum, CACHEFILE cf) {
    if (max_cf_pairs==0) {
	n_cf_pairs=1;
	max_cf_pairs=2;
	MALLOC_N(max_cf_pairs, cf_pairs);
	if (cf_pairs==0) return errno;
    } else {
	if (n_cf_pairs>=max_cf_pairs) {
	    max_cf_pairs*=2;
	    cf_pairs = toku_realloc(cf_pairs, max_cf_pairs*sizeof(*cf_pairs));
	}
	n_cf_pairs++;
    }
    cf_pairs[n_cf_pairs-1].filenum = fnum;
    cf_pairs[n_cf_pairs-1].cf      = cf;
    cf_pairs[n_cf_pairs-1].brt     = 0;
    return 0;
}

static int find_cachefile (FILENUM fnum, struct cf_pair **cf_pair) {
    int i;
    for (i=0; i<n_cf_pairs; i++) {
	if (fnum.fileid==cf_pairs[i].filenum.fileid) {
	    *cf_pair = cf_pairs+i;
	    return 0;
	}
    }
    return 1;
}

static char *fixup_fname(BYTESTRING *f) {
    assert(f->len>0);
    char *fname = toku_malloc(f->len+1);
    memcpy(fname, f->data, f->len);
    fname[f->len]=0;
    return fname;
}


void toku_recover_commit (LSN UU(lsn), TXNID UU(txnid)) {
}

// Rolling back a commit doesn't make much sense.  It won't happen during an abort.  But it doesn't hurt to ignore it.
int toku_rollback_commit (struct logtype_commit *le __attribute__((__unused__)), TOKUTXN txn __attribute__((__unused__))) {
    return 0;
}

#define ABORTIT { le=le; txn=txn; fprintf(stderr, "%s:%d (%s) not ready to go\n", __FILE__, __LINE__, __func__); abort(); }
int toku_rollback_delete (struct logtype_delete *le, TOKUTXN txn)               ABORTIT
void toku_recover_delete (LSN UU(lsn), FILENUM UU(filenum),DISKOFF UU(diskoff),BYTESTRING UU(key),BYTESTRING UU(data)) { assert(0); }

void toku_recover_fcreate (LSN UU(lsn), TXNID UU(txnid),BYTESTRING fname,u_int32_t mode) {
    char *fixed_fname = fixup_fname(&fname);
    int fd = creat(fixed_fname, mode);
    assert(fd>=0);
    toku_free(fixed_fname);
    toku_free_BYTESTRING(fname);
}

int toku_rollback_fcreate (TXNID      txnid     __attribute__((__unused__)),
			   BYTESTRING bs_fname,
			   u_int32_t  mode      __attribute__((__unused__)),
			   TOKUTXN    txn       __attribute__((__unused__))) {
    char *fname = fixup_fname(&bs_fname);
    char *directory = txn->logger->directory;
    int  full_len=strlen(fname)+strlen(directory)+2;
    char full_fname[full_len];
    int l = snprintf(full_fname,full_len, "%s/%s", directory, fname);
    assert(l<=full_len);
    int r = unlink(full_fname);
    assert(r==0);
    free(fname);
    return 0;
}


void toku_recover_fheader (LSN UU(lsn), TXNID UU(txnid),FILENUM filenum,LOGGEDBRTHEADER header) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    struct brt_header *MALLOC(h);
    assert(h);
    h->dirty=0;
    h->flags = header.flags;
    h->nodesize = header.nodesize;
    h->freelist = header.freelist;
    h->unused_memory = header.unused_memory;
    h->n_named_roots = header.n_named_roots;
    if ((signed)header.n_named_roots==-1) {
	h->unnamed_root = header.u.one.root;
    } else {
	assert(0);
    }
    toku_cachetable_put(pair->cf, 0, h, 0, toku_brtheader_flush_callback, toku_brtheader_fetch_callback, 0);
    if (pair->brt) {
	free(pair->brt->h);
    }  else {
	MALLOC(pair->brt);
	pair->brt->cf = pair->cf;
	pair->brt->database_name = 0; // Special case, we don't know or care what the database name is for recovery.
	list_init(&pair->brt->cursors);
	pair->brt->compare_fun = 0;
	pair->brt->dup_compare = 0;
	pair->brt->db = 0;
	pair->brt->skey = pair->brt->sval = 0;
    }
    pair->brt->h = h;
    pair->brt->nodesize = h->nodesize;
    pair->brt->flags    = h->nodesize;
    r = toku_unpin_brt_header(pair->brt);
    assert(r==0);
}

void toku_recover_newbrtnode (LSN lsn, FILENUM filenum,DISKOFF diskoff,u_int32_t height,u_int32_t nodesize,u_int8_t is_dup_sort,u_int32_t rand4fingerprint) {
    int r;
    struct cf_pair *pair;
    r = find_cachefile(filenum, &pair);
    assert(r==0);
    TAGMALLOC(BRTNODE, n);
    n->nodesize     = nodesize;
    n->thisnodename = diskoff;
    n->log_lsn = n->disk_lsn  = lsn;
    //printf("%s:%d %p->disk_lsn=%"PRId64"\n", __FILE__, __LINE__, n, n->disk_lsn.lsn);
    n->layout_version = 2;
    n->height         = height;
    n->rand4fingerprint = rand4fingerprint;
    n->flags = is_dup_sort ? TOKU_DB_DUPSORT : 0; // Don't have TOKU_DB_DUP ???
    n->local_fingerprint = 0; // nothing there yet
    n->dirty = 1;
    if (height==0) {
	r=toku_pma_create(&n->u.l.buffer, toku_dont_call_this_compare_fun, null_db, filenum, nodesize);
	assert(r==0);
	n->u.l.n_bytes_in_buffer=0;
    } else {
	n->u.n.n_children = 0;
	n->u.n.totalchildkeylens = 0;
	n->u.n.n_bytes_in_buffers = 0;
	int i;
	for (i=0; i<TREE_FANOUT+1; i++)
	    BNC_NBYTESINBUF(n, i) = 0;
    }
    // Now put it in the cachetable
    toku_cachetable_put(pair->cf, diskoff, n, toku_serialize_brtnode_size(n),  toku_brtnode_flush_callback, toku_brtnode_fetch_callback, 0);

    VERIFY_COUNTS(n);

    n->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(n));
    assert(r==0);
}

int toku_rollback_newbrtnode (struct logtype_newbrtnode *le, TOKUTXN txn) {
    // All that must be done is to put the node on the freelist.
    // Since we don't have a freelist right now, we don't have anything to do.
    // We'll fix this later (See #264)
    le=le;
    txn=txn;
    return 0;
}


static void recover_setup_node (FILENUM filenum, DISKOFF diskoff, CACHEFILE *cf, BRTNODE *resultnode) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    assert(pair->brt);
    void *node_v;
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    *resultnode = node;
    *cf = pair->cf;
}

int toku_rollback_brtdeq (struct logtype_brtdeq * le, TOKUTXN txn) ABORTIT
void toku_recover_brtdeq (LSN UU(lsn), FILENUM UU(filenum), DISKOFF UU(diskoff), u_int32_t UU(childnum), TXNID UU(xid), u_int32_t UU(typ), BYTESTRING UU(key), BYTESTRING UU(data), u_int32_t UU(oldfingerprint), u_int32_t UU(newfingerprint)) { assert(0); }

int toku_rollback_brtenq (struct logtype_brtenq * le, TOKUTXN txn) ABORTIT
void toku_recover_brtenq (LSN UU(lsn), FILENUM UU(filenum), DISKOFF UU(diskoff), u_int32_t UU(childnum), TXNID UU(xid), u_int32_t UU(typ), BYTESTRING UU(key), BYTESTRING UU(data), u_int32_t UU(oldfingerprint), u_int32_t UU(newfingerprint)) { assert(0); }

int toku_rollback_addchild (struct logtype_addchild *le, TOKUTXN txn) ABORTIT
void toku_recover_addchild (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t childnum, DISKOFF child, u_int32_t childfingerprint) {
    CACHEFILE cf;
    BRTNODE node;
    recover_setup_node(filenum, diskoff, &cf, &node);
    assert(node->height>0);
    assert(childnum <= (unsigned)node->u.n.n_children);
    unsigned int i;
    for (i=node->u.n.n_children; i>childnum; i--) {
	node->u.n.childinfos[i]=node->u.n.childinfos[i-1];
	BNC_NBYTESINBUF(node,i) = BNC_NBYTESINBUF(node,i-1);
	assert(i>=2);
	node->u.n.childkeys [i-1]      = node->u.n.childkeys [i-2];
    }
    if (childnum>0) {
	node->u.n.childkeys [childnum-1] = 0;
    }
    BNC_DISKOFF(node, childnum) = child;
    BNC_SUBTREE_FINGERPRINT(node, childnum) = childfingerprint;
    int r= toku_fifo_create(&BNC_BUFFER(node, childnum)); assert(r==0);
    BNC_NBYTESINBUF(node, childnum) = 0;
    node->u.n.n_children++;
    node->log_lsn = lsn;
    r = toku_cachetable_unpin(cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
}

int toku_rollback_delchild (struct logtype_delchild * le, TOKUTXN txn) ABORTIT
void toku_recover_delchild (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t childnum, DISKOFF child, u_int32_t childfingerprint, BYTESTRING pivotkey) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height>0);

    assert(childnum < (unsigned)node->u.n.n_children);
    assert(node->u.n.childinfos[childnum].subtree_fingerprint == childfingerprint);
    assert(BNC_DISKOFF(node, childnum)==child);
    assert(toku_fifo_n_entries(BNC_BUFFER(node,childnum))==0);
    assert(BNC_NBYTESINBUF(node,childnum)==0);
    assert(node->u.n.n_children>2); // Must be at least two children.
    u_int32_t i;
    assert(childnum>0);
    node->u.n.totalchildkeylens -= toku_brt_pivot_key_len(pair->brt, node->u.n.childkeys[childnum-1]);
    toku_free((void*)node->u.n.childkeys[childnum-1]);
    toku_fifo_free(&BNC_BUFFER(node,childnum));
    for (i=childnum+1; i<(unsigned)node->u.n.n_children; i++) {
	node->u.n.childinfos[i-1] = node->u.n.childinfos[i];
	BNC_NBYTESINBUF(node,i-1) = BNC_NBYTESINBUF(node,i);
	node->u.n.childkeys[i-2] = node->u.n.childkeys[i-1];
    }
    node->u.n.n_children--;

    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
    toku_free(pivotkey.data);
}

int toku_rollback_setchild (struct logtype_setchild *le, TOKUTXN txn) ABORTIT
void toku_recover_setchild (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t childnum, DISKOFF UU(oldchild), DISKOFF newchild) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height>0);
    assert(childnum < (unsigned)node->u.n.n_children);
    BNC_DISKOFF(node, childnum) = newchild;
    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
}
int toku_rollback_setpivot (struct logtype_setpivot *le, TOKUTXN txn) ABORTIT
void toku_recover_setpivot (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t childnum, BYTESTRING pivotkey) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height>0);
    
    struct kv_pair *new_pivot = kv_pair_malloc(pivotkey.data, pivotkey.len, 0, 0);

    node->u.n.childkeys[childnum] = new_pivot;
    node->u.n.totalchildkeylens += toku_brt_pivot_key_len(pair->brt, node->u.n.childkeys[childnum]);

    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);

    toku_free(pivotkey.data);
}

void toku_recover_changechildfingerprint (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t childnum, u_int32_t UU(oldfingerprint), u_int32_t newfingerprint) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height>0);
    assert((signed)childnum < node->u.n.n_children);
    BNC_SUBTREE_FINGERPRINT(node, childnum) = newfingerprint;
    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
    
}
int toku_rollback_changechildfingerprint (struct logtype_changechildfingerprint *le, TOKUTXN txn) ABORTIT

void toku_recover_fopen (LSN UU(lsn), TXNID UU(txnid), BYTESTRING fname, FILENUM filenum) {
    char *fixedfname = fixup_fname(&fname);
    CACHEFILE cf;
    int fd = open(fixedfname, O_RDWR, 0);
    assert(fd>=0);
    int r = toku_cachetable_openfd(&cf, ct, fd);
    assert(r==0);
    toku_recover_note_cachefile(filenum, cf);
    toku_free(fixedfname);
    toku_free_BYTESTRING(fname);
}

int toku_rollback_fopen (struct logtype_fopen *le  __attribute__((__unused__)), TOKUTXN txn  __attribute__((__unused__))) {
    // Nothing needs to be done to undo an fopen.
    return 0;
}

void toku_recover_insertinleaf (LSN lsn, TXNID UU(txnid), FILENUM filenum, DISKOFF diskoff, u_int32_t pmaidx, BYTESTRING keybs, BYTESTRING databs) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height==0);
    VERIFY_COUNTS(node);
    DBT key,data;
    r = toku_pma_set_at_index(node->u.l.buffer, pmaidx, toku_fill_dbt(&key, keybs.data, keybs.len), toku_fill_dbt(&data, databs.data, databs.len));
    assert(r==0);
    node->local_fingerprint += node->rand4fingerprint*toku_calccrc32_kvpair(keybs.data, keybs.len, databs.data, databs.len);
    node->u.l.n_bytes_in_buffer += PMA_ITEM_OVERHEAD + KEY_VALUE_OVERHEAD + keybs.len + databs.len; 

    VERIFY_COUNTS(node);

    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
    toku_free_BYTESTRING(keybs);
    toku_free_BYTESTRING(databs);
}

int toku_rollback_insertinleaf (TXNID txnid __attribute__((__unused__)), FILENUM filenum, DISKOFF diskoff, u_int32_t pmaidx, BYTESTRING key, BYTESTRING data, TOKUTXN txn)  {
    CACHEFILE cf;
    BRT brt;
    void *node_v;
    printf("Rollback insertinleaf\n");
    int r = toku_cachefile_of_filenum(txn->logger->ct, filenum, &cf, &brt);
    assert(r==0);
    r = toku_cachetable_get_and_pin(cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, brt);
    if (r!=0) return r;
    BRTNODE node = node_v;
    r = toku_pma_clear_at_index(node->u.l.buffer, pmaidx);
    if (r!=0) return r;
    node->local_fingerprint -= node->rand4fingerprint*toku_calccrc32_kvpair(key.data, key.len, data.data, data.len);
    node->u.l.n_bytes_in_buffer -= PMA_ITEM_OVERHEAD + KEY_VALUE_OVERHEAD + key.len + data.len; 
    VERIFY_COUNTS(node);
    //node->log_lsn = c->lsn;
    r = toku_cachetable_unpin(cf, diskoff, 1, toku_serialize_brtnode_size(node));
    return r;
}


void toku_recover_deleteinleaf (LSN lsn, TXNID UU(txnid), FILENUM filenum, DISKOFF diskoff, u_int32_t pmaidx, BYTESTRING keybs, BYTESTRING databs) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height==0);
    VERIFY_COUNTS(node);
    r = toku_pma_clear_at_index(node->u.l.buffer, pmaidx);
    assert (r==0);
    node->local_fingerprint -= node->rand4fingerprint*toku_calccrc32_kvpair(keybs.data, keybs.len, databs.data, databs.len);
    node->u.l.n_bytes_in_buffer -= PMA_ITEM_OVERHEAD + KEY_VALUE_OVERHEAD + keybs.len + databs.len; 
    VERIFY_COUNTS(node);
    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
    toku_free_BYTESTRING(keybs);
    toku_free_BYTESTRING(databs);
}

void toku_recover_tl_delete (LSN lsn __attribute__((__unused__)), FILENUM filenum  __attribute__((__unused__)), BYTESTRING key __attribute__((__unused__)), BYTESTRING data  __attribute__((__unused__))) {
    return; // tl_delete should not appear in the log.

}


int toku_rollback_tl_delete (FILENUM filenum,
			     BYTESTRING key,BYTESTRING data,TOKUTXN txn) {
    CACHEFILE cf;
    BRT brt;
    int r = toku_cachefile_of_filenum(txn->logger->ct, filenum, &cf, &brt);
    assert(r==0);
    DBT key_dbt,data_dbt;
    r = toku_brt_insert(brt,
			toku_fill_dbt(&key_dbt, key.data, key.len),
			toku_fill_dbt(&data_dbt, data.data, data.len),
			txn);
    return r;
}

// a newbrtnode should have been done before this
void toku_recover_resizepma (LSN lsn, FILENUM filenum, DISKOFF diskoff, u_int32_t oldsize, u_int32_t newsize) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_v;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin (pair->cf, diskoff, &node_v, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE node = node_v;
    assert(node->height==0);
    r = toku_resize_pma_exactly (node->u.l.buffer, oldsize, newsize);
    assert(r==0);
    
    VERIFY_COUNTS(node);

    node->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, diskoff, 1, toku_serialize_brtnode_size(node));
    assert(r==0);
}

void toku_recover_pmadistribute (LSN lsn, FILENUM filenum, DISKOFF old_diskoff, DISKOFF new_diskoff, INTPAIRARRAY fromto) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    void *node_va, *node_vb;
    assert(pair->brt);
    r = toku_cachetable_get_and_pin(pair->cf, old_diskoff, &node_va, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    r = toku_cachetable_get_and_pin(pair->cf, new_diskoff, &node_vb, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, pair->brt);
    assert(r==0);
    BRTNODE nodea = node_va;      assert(nodea->height==0);
    BRTNODE nodeb = node_vb;      assert(nodeb->height==0);
    {    
	unsigned int i;
	for (i=0; i<fromto.size; i++) {
	    assert(fromto.array[i].a < toku_pma_index_limit(nodea->u.l.buffer));
	    assert(fromto.array[i].b < toku_pma_index_limit(nodeb->u.l.buffer));
	}
    }
    r = toku_pma_move_indices (nodea->u.l.buffer, nodeb->u.l.buffer, fromto,
			       nodea->rand4fingerprint, &nodea->local_fingerprint,
			       nodeb->rand4fingerprint, &nodeb->local_fingerprint,
			       &nodea->u.l.n_bytes_in_buffer, &nodeb->u.l.n_bytes_in_buffer
			       );
    // The bytes in buffer and fingerprint shouldn't change

    VERIFY_COUNTS(nodea);
    VERIFY_COUNTS(nodeb);

    nodea->log_lsn = lsn;
    nodeb->log_lsn = lsn;
    r = toku_cachetable_unpin(pair->cf, old_diskoff, 1, toku_serialize_brtnode_size(nodea));
    assert(r==0);
    r = toku_cachetable_unpin(pair->cf, new_diskoff, 1, toku_serialize_brtnode_size(nodeb));
    assert(r==0);


    toku_free_INTPAIRARRAY(fromto);
}

int toku_rollback_pmadistribute (struct logtype_pmadistribute *le, TOKUTXN txn) {
    CACHEFILE cf;
    BRT brt;
    int r = toku_cachefile_of_filenum(txn->logger->ct, le->filenum, &cf, &brt);
    if (r!=0) return r;
    void *node_va, *node_vb;
    r = toku_cachetable_get_and_pin(cf, le->old_diskoff, &node_va, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, brt);
    if (r!=0) return r;
    if (0) {
    died0: toku_cachetable_unpin(cf, le->old_diskoff, 1, 0);
	return r;
    }
    r = toku_cachetable_get_and_pin(cf, le->new_diskoff, &node_vb, NULL, toku_brtnode_flush_callback, toku_brtnode_fetch_callback, brt);
    if (r!=0) goto died0;
    if (0) {
    died1:
	toku_cachetable_unpin(cf, le->new_diskoff, 1, 0);
	goto died0;
    }
    BRTNODE nodea = node_va;
    BRTNODE nodeb = node_vb;
    r = toku_pma_move_indices_back(nodea->u.l.buffer, nodeb->u.l.buffer, le->fromto,
				   nodeb->rand4fingerprint, &nodeb->local_fingerprint,
				   nodea->rand4fingerprint, &nodea->local_fingerprint,
				   &nodeb->u.l.n_bytes_in_buffer, &nodea->u.l.n_bytes_in_buffer
				   );
    if (r!=0) goto died1;
    nodea->log_lsn = le->lsn;
    nodeb->log_lsn = le->lsn;
    r = toku_cachetable_unpin(cf, le->old_diskoff, 1, toku_serialize_brtnode_size(nodea));
    r = toku_cachetable_unpin(cf, le->new_diskoff, 1, toku_serialize_brtnode_size(nodeb));

    return r;
}

int toku_rollback_fheader (struct logtype_fheader *le, TOKUTXN txn)             ABORTIT
int toku_rollback_resizepma (struct logtype_resizepma *le, TOKUTXN txn)         ABORTIT

void toku_recover_changeunnamedroot (LSN UU(lsn), FILENUM filenum, DISKOFF UU(oldroot), DISKOFF newroot) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    assert(pair->brt);
    r = toku_read_and_pin_brt_header(pair->cf, &pair->brt->h);
    assert(r==0);
    pair->brt->h->unnamed_root = newroot;
    r = toku_unpin_brt_header(pair->brt);
}
void toku_recover_changenamedroot (LSN UU(lsn), FILENUM UU(filenum), BYTESTRING UU(name), DISKOFF UU(oldroot), DISKOFF UU(newroot)) { assert(0); }
int toku_rollback_changeunnamedroot (struct logtype_changeunnamedroot *le, TOKUTXN txn) ABORTIT
int toku_rollback_changenamedroot (struct logtype_changenamedroot *le, TOKUTXN txn) ABORTIT

void toku_recover_changeunusedmemory (LSN UU(lsn), FILENUM filenum, DISKOFF UU(oldunused), DISKOFF newunused) {
    struct cf_pair *pair;
    int r = find_cachefile(filenum, &pair);
    assert(r==0);
    assert(pair->brt);
    r = toku_read_and_pin_brt_header(pair->cf, &pair->brt->h);
    assert(r==0);
    pair->brt->h->unused_memory = newunused;
    r = toku_unpin_brt_header(pair->brt);
}
int toku_rollback_changeunusedmemory (struct logtype_changeunusedmemory *le, TOKUTXN txn) ABORTIT

