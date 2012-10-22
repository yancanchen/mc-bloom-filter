#ifndef __BLOOM_FILTER_H__
#define __BLOOM_FILTER_H__

#include <stdio.h>
#include "list.h"
#include "debug.h"

#define ERROR_MALLOC_FAIL 1
#define OK 0

#ifndef MAX_KEY_LEN
#define MAX_KEY_LEN 32
#endif

typedef unsigned long long UINT64_RC;

typedef struct _bloom{
    struct list_head list;
    struct _bloom *hash_next;
    size_t bytes;
    char key[MAX_KEY_LEN];
    UINT64_RC n;
    UINT64_RC m;
    UINT64_RC get_count;
    UINT64_RC set_count;
    UINT64_RC get_miss_count;
    double e;
    double e2;
    int k;
    int map[0];
} bloom_t;

typedef struct {
    struct list_head list;
    size_t bytes;
    size_t max_bytes;
    int count;
    int power;
    UINT64_RC get_count;
    UINT64_RC set_count;
    UINT64_RC get_miss_count;
    bloom_t *hashtable[0];
} blooms_t;


int blooms_init (size_t max);
int blooms_add (char *key, UINT64_RC n, double e);
int blooms_set (char *key, char *set_key);
int blooms_get (char *key, char *get_key);
void blooms_status ();
int bloom_status ();
bloom_t *blooms_search(char *key);

int blooms_delete (char *key);
size_t calculate (UINT64_RC n, double e, UINT64_RC *m, int *k, double *tmp_e);
#endif
