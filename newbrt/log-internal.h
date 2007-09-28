#include "yerror.h"
#include <stdio.h>
#include "log.h"
#include <sys/types.h>


#define LOGGER_BUF_SIZE (1<<20)
struct tokulogger {
    enum typ_tag tag;
    char *directory;
    int fd;
    int n_in_file;
    long long next_log_file_number;
    char buf[LOGGER_BUF_SIZE];
    int  n_in_buf;
};

int tokulogger_find_next_unused_log_file(const char *directory, long long *result);

enum { LT_INSERT_WITH_NO_OVERWRITE = 'I', LT_DELETE = 'D', LT_COMMIT = 'C' };

struct tokutxn {
    u_int64_t txnid64;
    TOKULOGGER logger;
};
