#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bloom_filter.h"
#include "murmur.h"

#define BITSPERWORD 32  
#define SHIFT 5  
#define MASK 0x1F  

#define HASH_POWER 20
#define SALT_CONSTANT 0x97c29b3a

#define hashsize(power) ((unsigned int)1<<(power))
#define hashmask(power) (hashsize(power)-1)

#define FORCE_INLINE inline static

blooms_t *blooms = NULL;

/* Generic hash function (a popular one from Bernstein).  89  * I tested a few and this was the best. */
unsigned int dictGenHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = 5381;

    while (len--)
        hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
    return hash;
}

FORCE_INLINE void bitmap_set(int *map, UINT64_RC i) 
{
    map[i>>SHIFT] |= (1<<(i & MASK)); 
}  

FORCE_INLINE void bitmap_clear(int *map, UINT64_RC i) 
{ 
    map[i>>SHIFT] &= ~(1<<(i & MASK)); 
}  

FORCE_INLINE int  bitmap_check(int *map, UINT64_RC i)
{ 
    return map[i>>SHIFT] & (1<<(i & MASK)); 
}  

size_t calculate (UINT64_RC n, double e, UINT64_RC* m, int* k, double* tmp_e)
{
    double te = 1;
    int kk = 0;
    int x;
 
    for(x = 2; te > e && x < 100; x++) {
        kk = ceil(x * log(2));
        te =  pow((1 - exp((double)(-kk) / x)), kk);
    }

    /* overflow check. */
    if ((double)(x - 1) * n > (UINT64_RC)(-1)) return 0;

    UINT64_RC mm = (x - 1) * n;
    UINT64_RC bytes = (((mm + 0x1F) & (~0x1FULL)) >> 3) + sizeof(bloom_t);

    /* overflow check. */
    if (bytes > (size_t)(-1)) return 0;

    *k = kk;
    *tmp_e = te;
    *m = mm;

    return (size_t)bytes;
}


bloom_t *bloom_init (char * key, UINT64_RC n, double e)
{

    UINT64_RC m = 0;
    int k = 0;
    double tmp_e = 1;
    
    size_t bytes = calculate(n, e, &m, &k, &tmp_e);

    if (!bytes) {
        DEBUG("Error, bad arg n:%llu e:%lf\r\n", n, e);
        return NULL;
    }
    if(bytes > blooms->max_bytes - blooms->bytes){
        DEBUG("Error, bloom over max size(bytes:%lu, x:%llu m:%llu, e:%e)\r\n", (unsigned long)bytes, m/n, m, e);
        return NULL;
    }

    bloom_t *bloom = (bloom_t *)malloc(bytes); 
    if(!bloom) {
        DEBUG("Error, bloom init failed (malloc)\r\n");
        return NULL;
    }
    
    memset(bloom, 0, bytes);  
    bloom->m = m;
    bloom->e = e;
    bloom->e2 = tmp_e;
    bloom->k = k;
    bloom->n = n;
    bloom->bytes = bytes;
    strcpy(bloom->key, key);
    
    return bloom;
}

int bloom_set (bloom_t *bloom, char *key) 
{
    UINT64_RC checksum[2];

    MurmurHash3_x64_128(key, strlen(key), SALT_CONSTANT, checksum);
    UINT64_RC h1 = checksum[0];
    UINT64_RC h2 = checksum[1];

    int i;
    for (i = 0; i < bloom->k; i++) {
        UINT64_RC hashes = (h1 + i * h2) % bloom->m;
        bitmap_set(bloom->map, hashes);
    }
    bloom->set_count++;
    return 0;
}

int bloom_get (bloom_t *bloom, char *key) 
{
    UINT64_RC checksum[2];

    MurmurHash3_x64_128(key, strlen(key), SALT_CONSTANT, checksum);
    UINT64_RC h1 = checksum[0];
    UINT64_RC h2 = checksum[1];

    int i;
    for (i = 0; i < bloom->k; i++) {
        UINT64_RC hashes = (h1 + i * h2) % bloom->m;
        if(!bitmap_check(bloom->map, hashes)){
            bloom->get_miss_count++;
            blooms->get_miss_count++;
            return 0;
        }
    }
    bloom->get_count++;
    blooms->get_count++;
    return 1;
}


static void bloom_delete (bloom_t *bloom)
{
    list_del(&bloom->list);
    blooms->bytes -= bloom->bytes;
    free(bloom);
    blooms->count--;
}


bloom_t *blooms_search(char *key)
{
    unsigned int hv = dictGenHashFunction((unsigned char *)key, strlen(key)) & hashmask(blooms->power);

    bloom_t *bloom = blooms->hashtable[hv];

    while (bloom) {
        if (!strcmp(bloom->key, key)) {
            return bloom;
        }
        bloom = bloom->hash_next;
    }

    return NULL;
}

/**
 *1 key exist
 *2 bloom add fail
 *0 success
 */

int blooms_add (char *key, UINT64_RC n, double e)
{    
    unsigned int hv = dictGenHashFunction((unsigned char *)key, strlen(key)) & hashmask(blooms->power);
    bloom_t *bloom = blooms->hashtable[hv];

    while (bloom) {
        if (!strcmp(bloom->key, key)) {
            return 1;
        }
        bloom = bloom->hash_next;
    }

    bloom = bloom_init(key, n, e);
    if(!bloom) {
        DEBUG("Error, bloom init failed\r\n");
        return 2;
    }
    
    bloom->hash_next = blooms->hashtable[hv];
    blooms->hashtable[hv] = bloom;

    list_add_tail(&bloom->list, &blooms->list);
    
    blooms->count++;
    blooms->bytes += bloom->bytes;
    
    DEBUG("%s add\r\n", key);
    
    return 0;
}

/**
 *1 key not exist
 *0 success
 */

int blooms_set (char *key, char *subkey)
{
    bloom_t *bloom = blooms_search(key);
    if(!bloom) {
        DEBUG("Error, %s not exited\r\n", key);
        return 1;
    }

    bloom_set(bloom, subkey);
    blooms->set_count++;
    
    DEBUG("%s:\t%s\tOK\r\n", key, subkey);

    return 0;    
}

/**
 *-1 key not exist
 *1 success subkey exist
 *0 success subkey not exist
 */

int blooms_get (char *key, char *subkey)
{
    bloom_t *bloom = blooms_search(key);
    if(!bloom) {
        DEBUG("Error, %s not exited\r\n", key);
        return -1;
    }

    int result = bloom_get(bloom, subkey);
    
    DEBUG("%s:\t%s\t%d\r\n", key, subkey, result);

    return result;    
}


/**
 *-1 key not exist
 */

int bloom_status (char * key)
{
    bloom_t * bloom = blooms_search(key);
    if(!bloom) {
        DEBUG("Error, %s not exited\r\n", key);
        return -1;
    }

    printf("-----------------------------\r\n");
    printf("bloom filter name: %s\r\n", bloom->key);
    printf("expected max amount of elements: %llu\r\n", bloom->n);
    printf("expected false positive rate: %e\r\n", bloom->e);
    printf("theoretical false positive rate: %e\r\n", bloom->e2);
    printf("hash functions: %d\r\n", bloom->k);
    printf("table size (bits): %llu\r\n", bloom->m);
    printf("bloom size (bytes): %zu\r\n", bloom->bytes);
    printf("bloom set count: %llu\r\n", bloom->set_count);   
    printf("bloom get count: %llu\r\n", bloom->get_count);     
    printf("bloom get miss count: %llu\r\n", bloom->get_miss_count);    
    printf("-----------------------------\r\n\r\n");

    return 0;
}




void blooms_status ()
{
 
    printf("-----------------------------\r\n");
    if(!list_empty(&blooms->list)){
        printf("blooms list:\r\n");
        bloom_t * i;
        list_for_each_entry(i, &blooms->list, list){
            printf("\t\t%s\r\n", i->key);
        }
    }
    printf("blooms count: %d\r\n", blooms->count);    
    printf("blooms total bytes: %zu\r\n", blooms->bytes);
    printf("blooms set count: %llu\r\n", blooms->set_count);   
    printf("blooms get count: %llu\r\n", blooms->get_count);     
    printf("blooms get miss count: %llu\r\n", blooms->get_miss_count);     

    printf("-----------------------------\r\n\r\n");
}

/**
 *1 key not exist
 *0 success
 */

int blooms_delete (char *key)
{
    unsigned int hv = dictGenHashFunction((unsigned char *)key, strlen(key)) & hashmask(blooms->power);

    bloom_t *bloom = blooms->hashtable[hv];

    if(!bloom) {
        DEBUG("Error, %s not exist\r\n", key);
        return 1;
    }
    if (!strcmp(bloom->key, key)) {
        blooms->hashtable[hv] = bloom->hash_next;
        bloom_delete(bloom);
        DEBUG("%s deleted\r\n", key);
        return 0;
    } 

    bloom_t *next = bloom->hash_next;
    while (next) {

        if (!strcmp(next->key, key)) {
            bloom->hash_next = next->hash_next;
            bloom_delete(next);
            DEBUG("%s deleted\r\n", key);
            return 0;
        }

        bloom = bloom->hash_next;
        next = bloom->hash_next;
    }

    DEBUG("Error, %s not exist\r\n", key);
    return 1;
}


/**
 *1 blooms malloc fail
 *0 success
 */

int blooms_init (size_t max)
{
    size_t blooms_size = hashsize(HASH_POWER) * sizeof(blooms_t *) + sizeof(blooms_t);
    blooms = (blooms_t *)malloc(blooms_size);
    if(!blooms) {
        DEBUG("Error, blooms malloc failed\r\n");
        return 1;
    }
    memset(blooms, 0, blooms_size);  //fail?
    
    blooms->power = HASH_POWER;
    blooms->count = 0;
    blooms->bytes = 0;
    blooms->max_bytes = max;
    INIT_LIST_HEAD(&blooms->list);
    
    blooms->get_count = blooms->set_count = blooms->get_miss_count = 0;
    
    return 0;

}

