/* Make a db.h that will be link-time compatible with Sleepycat's Berkeley DB. */

#include <db.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


#define DECL_LIMIT 100
#define FIELD_LIMIT 100
struct fieldinfo {
    char decl[DECL_LIMIT];
    unsigned int off;
    unsigned int size;
} fields[FIELD_LIMIT];
int field_counter=0;


int compare_fields (const void *av, const void *bv) {
    const struct fieldinfo *a = av;
    const struct fieldinfo *b = bv;
    if (a->off < b->off) return -1;
    if (a->off > b->off) return 1;
    return 0;
}				      

#define STRUCT_SETUP(typ, name, fstring) ({ snprintf(fields[field_counter].decl, DECL_LIMIT, fstring, #name); \
	    fields[field_counter].off = __builtin_offsetof(typ, name);       \
            { typ dummy;                                           \
		fields[field_counter].size = sizeof(dummy.name); } \
	    field_counter++; })

FILE *outf;
void open_file (void) {
    char fname[100];
    snprintf(fname, 100, "sample_offsets_%d.h", __WORDSIZE);
    outf = fopen(fname, "w");
    assert(outf);

}

void sort_and_dump_fields (const char *structname, unsigned int sizeofstruct) {
    int i;
    qsort(fields, field_counter, sizeof(fields[0]), compare_fields);
    fprintf(outf, "struct fieldinfo %s_fields%d[] = {\n", structname, __WORDSIZE);
    for (i=0; i<field_counter; i++) {
	fprintf(outf, "  {\"%s\", %d, %d},\n", fields[i].decl, fields[i].off, fields[i].size);
    }
    fprintf(outf, "  {0, %d, %d} /* size of whole struct */\n", sizeofstruct, sizeofstruct);
    fprintf(outf, "};\n");
}

void sample_db_offsets (void) {
    /* Do these in alphabetical order. */
    field_counter=0;
    STRUCT_SETUP(DB,app_private,    "void *%s");
    STRUCT_SETUP(DB,close,          "int (*%s) (DB*, u_int32_t)");
    STRUCT_SETUP(DB,cursor,         "int (*%s) (DB *, DB_TXN *, DBC **, u_int32_t)");
    STRUCT_SETUP(DB,del,            "int (*%s) (DB *, DB_TXN *, DBT *, u_int32_t)");
    STRUCT_SETUP(DB,get,            "int (*%s) (DB *, DB_TXN *, DBT *, DBT *, u_int32_t)");
    STRUCT_SETUP(DB,key_range,      "int (*%s) (DB *, DB_TXN *, DBT *, DB_KEY_RANGE *, u_int32_t)");
    STRUCT_SETUP(DB,open,           "int (*%s) (DB *, DB_TXN *, const char *, const char *, DBTYPE, u_int32_t, int)");
    STRUCT_SETUP(DB,put,            "int (*%s) (DB *, DB_TXN *, DBT *, DBT *, u_int32_t)");
    STRUCT_SETUP(DB,remove,         "int (*%s) (DB *, const char *, const char *, u_int32_t)");
    STRUCT_SETUP(DB,rename,         "int (*%s) (DB *, const char *, const char *, const char *, u_int32_t)");
    STRUCT_SETUP(DB,set_bt_compare, "int (*%s) (DB *, int (*)(DB *, const DBT *, const DBT *))");
    STRUCT_SETUP(DB,set_flags,      "int (*%s) (DB *, u_int32_t)");
    STRUCT_SETUP(DB,stat,           "int (*%s) (DB *, void *, u_int32_t)");
    sort_and_dump_fields("db", sizeof(DB));
}

void sample_dbt_offsets (void) {
    field_counter=0;
    STRUCT_SETUP(DBT,app_private, "void*%s");
    STRUCT_SETUP(DBT,data,        "void*%s");
    STRUCT_SETUP(DBT,flags,       "u_int32_t %s");
    STRUCT_SETUP(DBT,size,        "u_int32_t %s");
    STRUCT_SETUP(DBT,ulen,        "u_int32_t %s");
    sort_and_dump_fields("dbt", sizeof(DBT));
}

void sample_db_txn_offsets (void) {
    field_counter=0;
    STRUCT_SETUP(DB_TXN, commit,      "int (*%s) (DB_TXN*, u_int32_t)");
    STRUCT_SETUP(DB_TXN, id,          "u_int32_t (*%s) (DB_TXN *)");
    sort_and_dump_fields("db_txn", sizeof(DB_TXN));
}

void sample_dbc_offsets (void) {
    field_counter=0;
    STRUCT_SETUP(DBC, c_close, "int (*%s) (DBC *)");
    STRUCT_SETUP(DBC, c_del,   "int (*%s) (DBC *, u_int32_t)");
    STRUCT_SETUP(DBC, c_get,   "int (*%s) (DBC *, DBT *, DBT *, u_int32_t)");
    sort_and_dump_fields("dbc", sizeof(DBC));
}

void sample_db_env_offsets (void) {
    field_counter=0;
    STRUCT_SETUP(DB_ENV, close, "int  (*%s) (DB_ENV *, u_int32_t)");
    STRUCT_SETUP(DB_ENV, err, "void (*%s) (const DB_ENV *, int, const char *, ...)");
    STRUCT_SETUP(DB_ENV, log_archive, "int  (*%s) (DB_ENV *, char **[], u_int32_t)");
    STRUCT_SETUP(DB_ENV, log_flush, "int  (*%s) (DB_ENV *, const DB_LSN *)");
    STRUCT_SETUP(DB_ENV, open, "int  (*%s) (DB_ENV *, const char *, u_int32_t, int)");
    STRUCT_SETUP(DB_ENV, set_cachesize, "int  (*%s) (DB_ENV *, u_int32_t, u_int32_t, int)");
    STRUCT_SETUP(DB_ENV, set_data_dir, "int  (*%s) (DB_ENV *, const char *)");
    STRUCT_SETUP(DB_ENV, set_errcall, "void (*%s) (DB_ENV *, void (*)(const char *, char *))");
    STRUCT_SETUP(DB_ENV, set_errpfx, "void (*%s) (DB_ENV *, const char *)");
    STRUCT_SETUP(DB_ENV, set_flags, "int  (*%s) (DB_ENV *, u_int32_t, int)");
    STRUCT_SETUP(DB_ENV, set_lg_bsize, "int  (*%s) (DB_ENV *, u_int32_t)");
    STRUCT_SETUP(DB_ENV, set_lg_dir, "int  (*%s) (DB_ENV *, const char *)");
    STRUCT_SETUP(DB_ENV, set_lg_max, "int  (*%s) (DB_ENV *, u_int32_t)");
    STRUCT_SETUP(DB_ENV, set_lk_detect, "int  (*%s) (DB_ENV *, u_int32_t)");
    STRUCT_SETUP(DB_ENV, set_lk_max, "int  (*%s) (DB_ENV *, u_int32_t)");
    STRUCT_SETUP(DB_ENV, set_noticecall, "void (*%s) (DB_ENV *, void (*)(DB_ENV *, db_notices))");
    STRUCT_SETUP(DB_ENV, set_tmp_dir, "int  (*%s) (DB_ENV *, const char *)");
    STRUCT_SETUP(DB_ENV, set_verbose, "int  (*%s) (DB_ENV *, u_int32_t, int)");
    STRUCT_SETUP(DB_ENV, txn_checkpoint, "int  (*%s) (DB_ENV *, u_int32_t, u_int32_t, u_int32_t)");
    STRUCT_SETUP(DB_ENV, txn_stat, "int  (*%s) (DB_ENV *, DB_TXN_STAT **, u_int32_t)");
    sort_and_dump_fields("db_env", sizeof(DB_ENV));
}



int main (int argc __attribute__((__unused__)), char *argv[] __attribute__((__unused__))) {
    open_file();
    fprintf(outf, "/* BDB offsets on a %d-bit machine */\n", __WORDSIZE);
    sample_db_offsets();
    sample_dbt_offsets();
    sample_db_txn_offsets();
    sample_dbc_offsets();
    sample_db_env_offsets();
    return 0;
}
