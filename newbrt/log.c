/* -*- mode: C; c-basic-offset: 4 -*- */
#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "brt-internal.h"
#include "log-internal.h"
#include "wbuf.h"
#include "memory.h"
#include "log_header.h"

int toku_logger_find_next_unused_log_file(const char *directory, long long *result) {
    DIR *d=opendir(directory);
    long long max=-1;
    struct dirent *de;
    if (d==0) return errno;
    while ((de=readdir(d))) {
	if (de==0) return errno;
	long long thisl;
	int r = sscanf(de->d_name, "log%llu.tokulog", &thisl);
	if (r==1 && thisl>max) max=thisl;
    }
    *result=max+1;
    int r = closedir(d);
    return r;
}

int toku_logger_find_logfiles (const char *directory, int *n_resultsp, char ***resultp) {
    int result_limit=1;
    int n_results=0;
    char **MALLOC_N(result_limit, result);
    struct dirent *de;
    DIR *d=opendir(directory);
    if (d==0) return errno;
    int dirnamelen = strlen(directory);
    while ((de=readdir(d))) {
	if (de==0) return errno;
	long long thisl;
	int r = sscanf(de->d_name, "log%llu.tokulog", &thisl);
	if (r!=1) continue; // Skip over non-log files.
	if (n_results>=result_limit) {
	    result_limit*=2;
	    result = toku_realloc(result, result_limit*sizeof(*result));
	}
	int fnamelen = dirnamelen + strlen(de->d_name) + 2; // One for the slash and one for the trailing NUL.
	char *fname = toku_malloc(fnamelen);
	snprintf(fname, fnamelen, "%s/%s", directory, de->d_name);
	result[n_results++] = fname;
    }
    *n_resultsp = n_results;
    *resultp    = result;
    return closedir(d);
}

int toku_logger_create (TOKULOGGER *resultp) {
    TAGMALLOC(TOKULOGGER, result);
    if (result==0) return errno;
    result->is_open=0;
    result->is_panicked=0;
    list_init(&result->live_txns);
    *resultp=result;
    return 0;
}

void toku_logger_set_cachetable (TOKULOGGER tl, CACHETABLE ct) {
    tl->ct = ct;
}


int toku_logger_open (const char *directory, TOKULOGGER logger) {
    if (logger->is_open) return EINVAL;
    if (logger->is_panicked) return EINVAL;
    int r;
    long long nexti;
    r = toku_logger_find_next_unused_log_file(directory, &nexti);
    if (r!=0) {
    died0:
	return r;
    }
    logger->directory = toku_strdup(directory);
    if (logger->directory==0) goto died0;
    logger->fd = -1;
    logger->next_log_file_number = nexti;
    logger->n_in_buf = 0;
    logger->n_in_file = 0;

    logger->lsn.lsn = 0; // WRONG!!!  This should actually be calculated by looking at the log file. 

    logger->is_open = 1;

    return toku_logger_log_bytes(logger, 0, "");
}

void toku_logger_panic (TOKULOGGER logger, int err) {
    logger->panic_errno=err;
    logger->is_panicked=1;
}
int toku_logger_panicked(TOKULOGGER logger) {
    if (logger==0) return 0;
    return logger->is_panicked;
}
int toku_logger_is_open(TOKULOGGER logger) {
    if (logger==0) return 0;
    return logger->is_open;
}

static int flush (TOKULOGGER logger) {
    if (logger->n_in_buf>0) {
	int r = write(logger->fd, logger->buf, logger->n_in_buf);
	if (r==-1) return errno;
	logger->n_in_file += logger->n_in_buf;
	logger->n_in_buf=0;
    }
    if (logger->n_in_file > 100<<20) {
	int r = close(logger->fd);
	if (r!=0) return errno;
	logger->fd=-1;
	logger->n_in_file = 0;
    }
    return 0;
}

static int log_format_version=0;

int toku_logger_log_bytes(TOKULOGGER logger, int nbytes, void *bytes) {
    if (logger->is_panicked) return EINVAL;
    //fprintf(stderr, "%s:%d logging %d bytes\n", __FILE__, __LINE__, nbytes);
    if (logger->fd==-1) {
	int  fnamelen = strlen(logger->directory)+50;
	char fname[fnamelen];
	snprintf(fname, fnamelen, "%s/log%012llu.tokulog", logger->directory, logger->next_log_file_number);
	//fprintf(stderr, "%s:%d creat(%s, ...)\n", __FILE__, __LINE__, fname);
	logger->fd = creat(fname, O_EXCL | 0700);
	if (logger->fd==-1) return errno;
	logger->next_log_file_number++;
	int version_l = htonl(log_format_version);
	int r = write(logger->fd, "tokulogg", 8); if (r!=8) return errno;
	r = write(logger->fd, &version_l, 4); if (r!=4) return errno;
    }
    if (logger->n_in_buf + nbytes > LOGGER_BUF_SIZE) {
	printf("flushing %d %d\n", logger->n_in_buf, logger->n_in_file);
	int r=flush(logger);
	if (r!=0) return r;
	if (nbytes>LOGGER_BUF_SIZE) {
	    r = write(logger->fd, bytes, nbytes);
	    if (r!=0) return errno;
	    logger->n_in_file = nbytes;
	    return flush(logger);
	}
	printf("saving %d\n", nbytes);
    }
    memcpy(logger->buf+logger->n_in_buf, bytes, nbytes);
    logger->n_in_buf += nbytes;
    return 0;
}

int toku_logger_close(TOKULOGGER *loggerp) {
    TOKULOGGER logger = *loggerp;
    if (logger->is_panicked) return EINVAL;
    int r = 0;
    if (!logger->is_open) goto is_closed;
    if (logger->fd!=-1) {
	//printf("%s:%d closing log: n_in_buf=%d\n", __FILE__, __LINE__, logger->n_in_buf);
	if (logger->n_in_buf>0) {
	    r = write(logger->fd, logger->buf, logger->n_in_buf);
	    if (r==-1) return errno;
	}
	r = fsync(logger->fd);
	if (r!=0)   close(logger->fd);
	else 	    r = close(logger->fd);
    }
    toku_free(logger->directory);
 is_closed:
    logger->is_panicked=1; // Just in case this might help.
    toku_free(logger);
    *loggerp=0;
    return r;
}
#if 0
int toku_logger_log_brt_remove (TOKULOGGER logger,
			       TXNID txnid,
			       diskoff diskoff,
			       unsigned char *key,
			       int keylen,
			       unsigned char *val,
			       int vallen) {
n    
}
#endif

int toku_logger_fsync (TOKULOGGER logger) {
    //return 0;/// NO TXN
    //fprintf(stderr, "%s:%d syncing log\n", __FILE__, __LINE__);
    if (logger->is_panicked) return EINVAL;
    int r=flush(logger);
    if (r!=0) return r;
    if (logger->fd>=0) {
	r = fsync(logger->fd);
	if (r!=0) return errno;
    }
    return 0;
}

int toku_logger_finish (TOKULOGGER logger, struct wbuf *wbuf) {
    if (logger->is_panicked) return EINVAL;
    wbuf_int(wbuf, toku_crc32(0, wbuf->buf, wbuf->ndone));
    wbuf_int(wbuf, 4+wbuf->ndone);
    return toku_logger_log_bytes(logger, wbuf->ndone, wbuf->buf);
}

int toku_logger_commit (TOKUTXN txn, int nosync) {
    // panic handled in log_commit
    int r = toku_log_commit(txn->logger, txn->txnid64);
    if (r!=0) goto free_and_return;
    if (txn->parent==0) {
	if (!nosync) {
	    r = toku_logger_fsync(txn->logger);
	    if (r!=0) toku_logger_panic(txn->logger, r);
	}
	goto free_and_return;
    } else {
	// Append the list to the front.
	if (txn->oldest_logentry) {
	    // There are some entries, so link them in.
	    txn->oldest_logentry->prev = txn->parent->newest_logentry;
	    txn->parent->newest_logentry = txn->newest_logentry;
	}
	if (txn->parent->oldest_logentry==0) {
	    txn->parent->oldest_logentry = txn->oldest_logentry;
	}
	txn->newest_logentry = txn->oldest_logentry = 0;
    }
 free_and_return: /*nothing*/;
    struct roll_entry *item;
    while ((item=txn->newest_logentry)) {
	txn->newest_logentry = item->prev;
	rolltype_dispatch(item, toku_free_rolltype_);
	toku_free(item);
    }
    list_remove(&txn->live_txns_link);
    toku_free(txn);
    return r;
}

int toku_logger_log_checkpoint (TOKULOGGER logger, LSN *lsn) {
    if (logger->is_panicked) return EINVAL;
    struct wbuf wbuf;
    const int buflen =10;
    unsigned char buf[buflen];
    wbuf_init(&wbuf, buf, buflen);
    wbuf_char(&wbuf, LT_CHECKPOINT);
    wbuf_LSN (&wbuf, logger->lsn);
    *lsn = logger->lsn;
    logger->lsn.lsn++;
    return toku_logger_log_bytes(logger, wbuf.ndone, wbuf.buf);
    
}

int toku_logger_txn_begin (TOKUTXN parent_tokutxn, TOKUTXN *tokutxn, TXNID txnid64, TOKULOGGER logger) {
    if (logger->is_panicked) return EINVAL;
    TAGMALLOC(TOKUTXN, result);
    if (result==0) return errno;
    result->txnid64 = txnid64;
    result->logger = logger;
    result->parent = parent_tokutxn;
    result->oldest_logentry = result->newest_logentry = 0;
    list_push(&logger->live_txns, &result->live_txns_link);
    *tokutxn = result;
    return 0;
}

int toku_logger_log_fcreate (TOKUTXN txn, const char *fname, int mode) {
    if (txn==0) return 0;
    if (txn->logger->is_panicked) return EINVAL;
    BYTESTRING bs = { .len=strlen(fname), .data = strdup(fname) };
    int r = toku_log_fcreate (txn->logger, toku_txn_get_txnid(txn), bs, mode);
    if (r!=0) return r;
    r = toku_logger_save_rollback_fcreate(txn, bs);
    return r;
}

/* fopen isn't really an action.  It's just for bookkeeping.  We need to know the filename that goes with a filenum. */
int toku_logger_log_fopen (TOKUTXN txn, const char * fname, FILENUM filenum) {
    if (txn==0) return 0;
    if (txn->logger->is_panicked) return EINVAL;
    BYTESTRING bs;
    bs.len = strlen(fname);
    bs.data = (char*)fname;
    return toku_log_fopen (txn->logger, toku_txn_get_txnid(txn), bs, filenum);
    
}

int toku_logger_log_header (TOKUTXN txn, FILENUM filenum, struct brt_header *h) {
    if (txn==0) return 0;
    if (txn->logger->is_panicked) return EINVAL;
#if 0
    LOGGEDBRTHEADER lh;
    lh.size = toku_serialize_brt_header_size(h);
    lh.flags = h->flags;
    lh.nodesize = h->nodesize;
    lh.freelist = h->freelist;
    lh.unused_memory = h->unused_memory;
    lh.n_named_roots = h->n_named_roots;
    if (h->n_named_roots==-1) {
	lh.u.one.root = h->unnamed_root;
    } else {
	int i;
	MALLOC_N(h->n_named_roots, lh.u.many.names);
	MALLOC_N(h->n_named_roots, lh.u.many.roots);
	for (i=0; i<h->n_named_roots; i++) {
	    lh.u.many.names[i]=toku_strdup(h->names[i]);
	    lh.u.many.roots[i]=h->roots[i];
	}
    }
    r = toku_log_fheader(txn, toku_txn_get_txnid(txn), filenum, lh);
    toku_free(all_that_stuff);
    return r;
#else
    int subsize=toku_serialize_brt_header_size(h);
    int buflen = (4   // firstlen
		  + 1 //cmd
		  + 8 // lsn
		  + 8 // txnid
		  + 4 // filenum
		  + subsize
		  + 8 // crc & len
		  );
    unsigned char *buf=toku_malloc(buflen); // alloc on heap because it might be big
    int r;
    if (buf==0) return errno;
    struct wbuf wbuf;
    wbuf_init(&wbuf, buf, buflen);
    wbuf_int (&wbuf, buflen);
    wbuf_char(&wbuf, LT_FHEADER);
    wbuf_LSN    (&wbuf, txn->logger->lsn);
    txn->logger->lsn.lsn++;
    wbuf_TXNID(&wbuf, txn->txnid64);
    wbuf_FILENUM(&wbuf, filenum);
    r=toku_serialize_brt_header_to_wbuf(&wbuf, h);
    if (r!=0) return r;
    r=toku_logger_finish(txn->logger, &wbuf);
    toku_free(buf);
    return r;
#endif
}

/*
int brtenv_checkpoint (BRTENV env) {
    init the checkpointing lock
    acquire_spinlock(&env->checkpointing);
    release_spinlock(&env->checkpointing);
    return -1;
}
*/

int toku_fread_u_int8_t_nocrclen (FILE *f, u_int8_t *v) {
    int vi=fgetc(f);
    if (vi==EOF) return -1;
    u_int8_t vc=vi;
    *v = vc;
    return 0;
}

int toku_fread_u_int8_t (FILE *f, u_int8_t *v, u_int32_t *crc, u_int32_t *len) {
    int vi=fgetc(f);
    if (vi==EOF) return -1;
    u_int8_t vc=vi;
    (*crc) = toku_crc32(*crc, &vc, 1);
    (*len)++;
    *v = vc;
    return 0;
}

int toku_fread_u_int32_t_nocrclen (FILE *f, u_int32_t *v) {
    u_int8_t c0,c1,c2,c3;
    int r;
    r = toku_fread_u_int8_t_nocrclen (f, &c0); if (r!=0) return r;
    r = toku_fread_u_int8_t_nocrclen (f, &c1); if (r!=0) return r;
    r = toku_fread_u_int8_t_nocrclen (f, &c2); if (r!=0) return r;
    r = toku_fread_u_int8_t_nocrclen (f, &c3); if (r!=0) return r;
    *v = ((c0<<24)|
	  (c1<<16)|
	  (c2<< 8)|
	  (c3<<0));
    return 0;
}
int toku_fread_u_int32_t (FILE *f, u_int32_t *v, u_int32_t *crc, u_int32_t *len) {
    u_int8_t c0,c1,c2,c3;
    int r;
    r = toku_fread_u_int8_t (f, &c0, crc, len); if(r!=0) return r;
    r = toku_fread_u_int8_t (f, &c1, crc, len); if(r!=0) return r;
    r = toku_fread_u_int8_t (f, &c2, crc, len); if(r!=0) return r;
    r = toku_fread_u_int8_t (f, &c3, crc, len); if(r!=0) return r;
    *v = ((c0<<24)|
	  (c1<<16)|
	  (c2<< 8)|
	  (c3<<0));
    return 0;
}

int toku_fread_u_int64_t (FILE *f, u_int64_t *v, u_int32_t *crc, u_int32_t *len) {
    u_int32_t v1,v2;
    int r;
    r=toku_fread_u_int32_t(f, &v1, crc, len);    if (r!=0) return r;
    r=toku_fread_u_int32_t(f, &v2, crc, len);    if (r!=0) return r;
    *v = (((u_int64_t)v1)<<32 ) | ((u_int64_t)v2);
    return 0;
}
int toku_fread_LSN     (FILE *f, LSN *lsn, u_int32_t *crc, u_int32_t *len) {
    return toku_fread_u_int64_t (f, &lsn->lsn, crc, len);
}
int toku_fread_FILENUM (FILE *f, FILENUM *filenum, u_int32_t *crc, u_int32_t *len) {
    return toku_fread_u_int32_t (f, &filenum->fileid, crc, len);
}
int toku_fread_DISKOFF (FILE *f, DISKOFF *diskoff, u_int32_t *crc, u_int32_t *len) {
    int r = toku_fread_u_int64_t (f, (u_int64_t*)diskoff, crc, len); // sign conversion will be OK.
    return r;
}
int toku_fread_TXNID   (FILE *f, TXNID *txnid, u_int32_t *crc, u_int32_t *len) {
    return toku_fread_u_int64_t (f, txnid, crc, len);
}
// fills in the bs with malloced data.
int toku_fread_BYTESTRING (FILE *f, BYTESTRING *bs, u_int32_t *crc, u_int32_t *len) {
    int r=toku_fread_u_int32_t(f, (u_int32_t*)&bs->len, crc, len);
    if (r!=0) return r;
    bs->data = toku_malloc(bs->len);
    int i;
    for (i=0; i<bs->len; i++) {
	r=toku_fread_u_int8_t(f, (u_int8_t*)&bs->data[i], crc, len);
	if (r!=0) {
	    toku_free(bs->data);
	    bs->data=0;
	    return r;
	}
    }
    return 0;
}

int toku_fread_LOGGEDBRTHEADER (FILE *f, LOGGEDBRTHEADER *v, u_int32_t *crc, u_int32_t *len) {
    int r;
    r = toku_fread_u_int32_t(f, &v->size,          crc, len); if (r!=0) return r;
    r = toku_fread_u_int32_t(f, &v->flags,         crc, len); if (r!=0) return r;
    r = toku_fread_u_int32_t(f, &v->nodesize,      crc, len); if (r!=0) return r;
    r = toku_fread_DISKOFF  (f, &v->freelist,      crc, len); if (r!=0) return r;
    r = toku_fread_DISKOFF  (f, &v->unused_memory, crc, len); if (r!=0) return r;
    r = toku_fread_u_int32_t(f, &v->n_named_roots, crc, len); if (r!=0) return r;
    assert((signed)v->n_named_roots==-1);
    r = toku_fread_DISKOFF  (f, &v->u.one.root,     crc, len); if (r!=0) return r;
    return 0;
}

int toku_fread_INTPAIRARRAY (FILE *f, INTPAIRARRAY *v, u_int32_t *crc, u_int32_t *len) {
    int r;
    u_int32_t i;
    r = toku_fread_u_int32_t(f, &v->size, crc, len); if (r!=0) return r;
    MALLOC_N(v->size, v->array);
    if (v->array==0) return errno;
    for (i=0; i<v->size; i++) {
	r = toku_fread_u_int32_t(f, &v->array[i].a, crc, len); if (r!=0) return r;
	r = toku_fread_u_int32_t(f, &v->array[i].b, crc, len); if (r!=0) return r;
    }
    return 0;
}

int toku_logprint_LSN (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    LSN v;
    int r = toku_fread_LSN(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s=%" PRId64, fieldname, v.lsn);
    return 0;
}
int toku_logprint_TXNID (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    TXNID v;
    int r = toku_fread_TXNID(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s=%" PRId64, fieldname, v);
    return 0;
}

int toku_logprint_u_int8_t (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format) {
    u_int8_t v;
    int r = toku_fread_u_int8_t(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s=%d", fieldname, v);
    if (format) fprintf(outf, format, v);
    else if (v=='\'') fprintf(outf, "('\'')");
    else if (isprint(v)) fprintf(outf, "('%c')", v);
    else {}/*nothing*/
    return 0;
    
}

int toku_logprint_u_int32_t (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format) {
    u_int32_t v;
    int r = toku_fread_u_int32_t(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s=", fieldname);
    fprintf(outf, format ? format : "%d", v);
    return 0;
    
}
int toku_logprint_BYTESTRING (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    BYTESTRING bs;
    int r = toku_fread_BYTESTRING(inf, &bs, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s={len=%d data=\"", fieldname, bs.len);
    int i;
    for (i=0; i<bs.len; i++) {
	switch (bs.data[i]) {
	case '"':  fprintf(outf, "\\\""); break;
	case '\\': fprintf(outf, "\\\\"); break;
	case '\n': fprintf(outf, "\\n");  break;
	default:
	    if (isprint(bs.data[i])) fprintf(outf, "%c", bs.data[i]);
	    else fprintf(outf, "\\%03o", bs.data[i]);
	}
    }
    fprintf(outf, "\"}");
    toku_free(bs.data);
    return 0;
}
int toku_logprint_FILENUM (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format) {
    return toku_logprint_u_int32_t(outf, inf, fieldname, crc, len, format);
    
}
int toku_logprint_DISKOFF (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    DISKOFF v;
    int r = toku_fread_DISKOFF(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s=%lld", fieldname, v);
    return 0;
}
int toku_logprint_LOGGEDBRTHEADER (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    LOGGEDBRTHEADER v;
    int r = toku_fread_LOGGEDBRTHEADER(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s={size=%d flags=%d nodesize=%d freelist=%lld unused_memory=%lld n_named_roots=%d", fieldname, v.size, v.flags, v.nodesize, v.freelist, v.unused_memory, v.n_named_roots);
    return 0;
    
}

int toku_logprint_INTPAIRARRAY (FILE *outf, FILE *inf, const char *fieldname, u_int32_t *crc, u_int32_t *len, const char *format __attribute__((__unused__))) {
    INTPAIRARRAY v;
    u_int32_t i;
    int r = toku_fread_INTPAIRARRAY(inf, &v, crc, len);
    if (r!=0) return r;
    fprintf(outf, " %s={size=%d array={", fieldname, v.size);
    for (i=0; i<v.size; i++) {
	if (i!=0) fprintf(outf, " ");
	fprintf(outf, "{%d %d}", v.array[i].a, v.array[i].b);
    }
    toku_free(v.array);
    return 0;
}

int toku_read_and_print_logmagic (FILE *f, u_int32_t *versionp) {
    {
	char magic[8];
	int r=fread(magic, 1, 8, f);
	if (r!=8) {
	    return DB_BADFORMAT;
	}
	if (memcmp(magic, "tokulogg", 8)!=0) {
	    return DB_BADFORMAT;
	}
    }
    {
	int version;
    	int r=fread(&version, 1, 4, f);
	if (r!=4) {
	    return DB_BADFORMAT;
	}
	//printf("tokulog v.%d\n", ntohl(version));
	*versionp=ntohl(version);
    }
    return 0;
}

TXNID toku_txn_get_txnid (TOKUTXN txn) {
    if (txn==0) return 0;
    else return txn->txnid64;
}

LSN toku_txn_get_last_lsn (TOKUTXN txn) {
    if (txn==0) return (LSN){0};
    return txn->last_lsn;
}
LSN toku_logger_last_lsn(TOKULOGGER logger) {
    LSN result=logger->lsn;
    result.lsn--;
    return result;
}

TOKULOGGER toku_txn_logger (TOKUTXN txn) {
    return txn ? txn->logger : 0;
}

int toku_abort_logentry_commit (struct logtype_commit *le __attribute__((__unused__)), TOKUTXN txn) {
    toku_logger_panic(txn->logger, EINVAL);
    return EINVAL;
}

int toku_logger_abort(TOKUTXN txn) {
    // Must undo everything.  Must undo it all in reverse order.
    // Build the reverse list
    struct roll_entry *item;
    while ((item=txn->newest_logentry)) {
	txn->newest_logentry = item->prev;
	int r;
	rolltype_dispatch_assign(item, toku_rollback_, r, txn);
	if (r!=0) return r;
	rolltype_dispatch(item, toku_free_rolltype_);
	toku_free(item);
    }
    list_remove(&txn->live_txns_link);
    toku_free(txn);
    return 0;
}

int toku_txnid2txn (TOKULOGGER logger, TXNID txnid, TOKUTXN *result) {
    if (logger==0) return -1;
    struct list *l;
    for (l = list_head(&logger->live_txns); l != &logger->live_txns; l = l->next) {
	TOKUTXN txn = list_struct(l, struct tokutxn, live_txns_link);
	assert(txn->tag==TYP_TOKUTXN);
	if (txn->txnid64==txnid) {
	    *result = txn;
	    return 0;
	}
    }
    // If there is no txn, then we treat it as the null txn.
    *result = 0;
    return 0;
}
