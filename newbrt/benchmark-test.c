/* Insert a bunch of stuff */
#include "brt.h"
#include "key.h"
#include "memory.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

const char fname[]="sinsert.brt";

enum { SERIAL_SPACING = 1<<6 };
enum { ITEMS_TO_INSERT_PER_ITERATION = 1<<20 };
//enum { ITEMS_TO_INSERT_PER_ITERATION = 1<<14 };
enum { BOUND_INCREASE_PER_ITERATION = SERIAL_SPACING*ITEMS_TO_INSERT_PER_ITERATION };

enum { NODE_SIZE = 1<<20 };

CACHETABLE ct;
BRT t;

void setup (int nodesize) {
    printf("nodesize=%d\n", nodesize);
    int r;
    unlink(fname);
    r = brt_create_cachetable(&ct, 0); assert(r==0);
    r = open_brt(fname, 0, 1, &t, nodesize, ct, default_compare_fun); assert(r==0);
}

void shutdown (void) {
    int r;
    r = close_brt(t); assert(r==0);
    r = cachetable_close(&ct); assert(r==0);
}
void long_long_to_array (unsigned char *a, unsigned long long l) {
    int i;
    for (i=0; i<8; i++)
	a[i] = (l>>(56-8*i))&0xff;
}

void insert (long long v) {
    unsigned char kc[8], vc[8];
    DBT  kt, vt;
    long_long_to_array(kc, v);
    long_long_to_array(vc, v);
    brt_insert(t, fill_dbt(&kt, kc, 8), fill_dbt(&vt, vc, 8), 0, 0);
}

void serial_insert_from (long long from) {
    long long i;
    for (i=0; i<ITEMS_TO_INSERT_PER_ITERATION; i++) {
	insert((from+i)*SERIAL_SPACING);
    }
}

long long llrandom (void) {
    return (((long long)(random()))<<32) + random();
}

void random_insert_below (long long below) {
    long long i;
    for (i=0; i<ITEMS_TO_INSERT_PER_ITERATION; i++) {
	insert(llrandom()%below);
    }
}

double tdiff (struct timeval *a, struct timeval *b) {
    return (a->tv_sec-b->tv_sec)+1e-6*(a->tv_usec-b->tv_usec);
}

void biginsert (long long n_elements, struct timeval *starttime) {
    long i;
    struct timeval t1,t2;
    int iteration;
    for (i=0, iteration=0; i<n_elements; i+=ITEMS_TO_INSERT_PER_ITERATION, iteration++) {
	gettimeofday(&t1,0);
	serial_insert_from(i);
	gettimeofday(&t2,0);
	printf("serial %9.6fs %8.0f/s    ", tdiff(&t2, &t1), ITEMS_TO_INSERT_PER_ITERATION/tdiff(&t2, &t1));
	fflush(stdout);
	gettimeofday(&t1,0);
	random_insert_below((i+ITEMS_TO_INSERT_PER_ITERATION)*SERIAL_SPACING);
	gettimeofday(&t2,0);
	printf("random %9.6fs %8.0f/s    ", tdiff(&t2, &t1), ITEMS_TO_INSERT_PER_ITERATION/tdiff(&t2, &t1));
	printf("cumulative %9.6fs %8.0f/s\n", tdiff(&t2, starttime), (ITEMS_TO_INSERT_PER_ITERATION*2.0/tdiff(&t2, starttime))*(iteration+1));
    }
}

void usage() {
    printf("benchmark-test [--nodesize NODESIZE] [TOTALITEMS]\n");
}

int main (int argc, char *argv[]) {
    /* set defaults */
    int nodesize = NODE_SIZE;

    /* parse parameters */
    int i;
    for (i=1; i<argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-')
            break;
        if (strcmp(arg, "--nodesize") == 0) {
            if (i+1 < argc) {
                i++;
                nodesize = atoi(argv[i]);
            }
            continue;
        }

        usage();
        return 1;
    }

    struct timeval t1,t2,t3;
    long long total_n_items;
    if (i < argc) {
	char *end;
	errno=0;
	total_n_items = ITEMS_TO_INSERT_PER_ITERATION * (long long) strtol(argv[i], &end, 10);
	assert(errno==0);
	assert(*end==0);
	assert(end!=argv[i]);
    } else {
	total_n_items = 1LL<<22; // 1LL<<16
    }

    printf("Serial and random insertions of %d per batch\n", ITEMS_TO_INSERT_PER_ITERATION);
    setup(nodesize);
    gettimeofday(&t1,0);
    biginsert(total_n_items, &t1);
    gettimeofday(&t2,0);
    shutdown();
    gettimeofday(&t3,0);
    printf("Shutdown %9.6fs\n", tdiff(&t3, &t2));
    printf("Total time %9.6fs for %lld insertions = %8.0f/s\n", tdiff(&t3, &t1), 2*total_n_items, 2*total_n_items/tdiff(&t3, &t1));
    malloc_report();
    malloc_cleanup();
    return 0;
}

