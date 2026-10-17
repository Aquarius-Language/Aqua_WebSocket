/*
 * aq_json.h - Small JSON reader/writer for the Aquarius library ABI.
 * MIT License, Copyright (c) 2026 Temple Lin.
 */
#ifndef AQ_JSON_H
#define AQ_JSON_H
#include "aq_plat.h"

typedef enum { AJ_NULL, AJ_BOOL, AJ_NUM, AJ_STR, AJ_ARR, AJ_OBJ } aj_type;
typedef struct aj {
    aj_type type;
    int boolean;
    double num;
    char *str;        /* UTF-8, NUL terminated (may contain embedded NULs; see len) */
    size_t len;
    char *key;        /* member name when inside an object */
    struct aj *child; /* first element/member */
    struct aj *next;  /* next sibling */
} aj;

aj *aj_parse(const char *text, size_t n);  /* NULL on malformed input */
void aj_free(aj *v);
aj *aj_get(const aj *obj, const char *key);
const char *aj_get_str(const aj *obj, const char *key, const char *def);
long long aj_get_int(const aj *obj, const char *key, long long def);
double aj_get_num(const aj *obj, const char *key, double def);
int aj_get_bool(const aj *obj, const char *key, int def);
int aj_count(const aj *arr);

/* Writers append to an aq_buf. */
void aj_w_str(aq_buf *b, const char *s, size_t n);
void aj_w_cstr(aq_buf *b, const char *s);
void aj_w_key(aq_buf *b, const char *key, int *first);   /* writes ,"key": */

#endif
