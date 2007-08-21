#ifndef RBUF_H
#define RBUF_H

#include <assert.h>

struct rbuf {
    unsigned char *buf;
    unsigned int  size;
    unsigned int  ndone;
};

static unsigned int rbuf_char (struct rbuf *r) {
    assert(r->ndone<r->size);
    return r->buf[r->ndone++];
}

static unsigned int rbuf_int (struct rbuf *r) {
    unsigned char c0 = rbuf_char(r);
    unsigned char c1 = rbuf_char(r);
    unsigned char c2 = rbuf_char(r);
    unsigned char c3 = rbuf_char(r);
    return ((c0<<24)|
	    (c1<<16)|
	    (c2<<8)|
	    (c3<<0));
}

/* Return a pointer into the middle of the buffer. */
static void rbuf_bytes (struct rbuf *r, bytevec *bytes, unsigned int *n_bytes)
{
    *n_bytes = rbuf_int(r);
    *bytes =   &r->buf[r->ndone];
    r->ndone+=*n_bytes;
    assert(r->ndone<=r->size);
}

static diskoff rbuf_diskoff (struct rbuf *r) {
    unsigned i0 = rbuf_int(r);  
    unsigned i1 = rbuf_int(r);
    return ((unsigned long long)(i0)<<32) | ((unsigned long long)(i1));
}
#endif
