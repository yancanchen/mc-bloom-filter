#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../bloom_filter.h"

/* basic functional test. */
int main(int argc, char *argv[])
{
    char *key = "dalfjabc";
    blooms_init(4294967296);

    assert(!blooms_add(key, 10000, 0.00001));
    blooms_status();

    assert(!blooms_set(key, "ddd"));
    assert(1 == blooms_get(key, "ddd"));

    assert(!blooms_set(key, "dde"));
    assert(1 == blooms_get(key, "dde"));

    assert(!blooms_get(key, "ddf"));
    assert(!blooms_delete(key));

    assert(1 == blooms_set(key, "ddd"));
    assert(-1 == blooms_get(key, "ddd"));

    assert(0 == blooms_add(key, 10000, 0.0001));
    assert(0 == blooms_set(key, "ddd"));
    assert(1 == blooms_get(key, "ddd"));
    assert(0 == blooms_get(key, "dde"));
    assert(0 == blooms_delete(key));

    assert(0 == blooms_add(key, 10000, 0.0001));
    assert(1 == blooms_add(key, 10000, 0.0001));

    assert(0 == blooms_set(key, "ddd"));
    assert(1 == blooms_get(key, "ddd"));
    assert(0 == blooms_get(key, "dde"));

    assert(0 == blooms_delete(key));

    assert(1 == blooms_delete(key));


    int i;
    for (i = 0; i < 1000; i ++) {
        char tmpkey[32];
        sprintf(tmpkey, "key%d", i);
        assert(0 == blooms_add(tmpkey, 10000, 0.0001));
    }
    blooms_status();

    for (i = 0; i < 1000; i ++) {
        char tmpkey[32];
        sprintf(tmpkey, "key%d", i);
        assert(0 == blooms_set(tmpkey, "ddd"));
        assert(1 == blooms_get(tmpkey, "ddd"));
    }

    for (i = 0; i < 1000; i ++) {
        char tmpkey[32];
        sprintf(tmpkey, "key%d", i);
        assert(0 == blooms_delete(tmpkey));
    }

    unsigned long long item_num = 1000000000;
    assert(!blooms_add(key, item_num, 0.00001));
    blooms_status();
    bloom_status(key);
    for (i = 0; i < item_num; i ++) {
        char tmpkey[32];
        sprintf(tmpkey, "subk%d", i);
        if (0 != blooms_get(key, tmpkey)) {
            fprintf(stderr, "conflict detected\r\n");
        }
        assert(0 == blooms_set(key, tmpkey));
    }

    for (i = 0; i < item_num; i ++) {
        char tmpkey[32];
        sprintf(tmpkey, "subk%d", i);
        assert(1 == blooms_get(key, tmpkey));
    }
    assert(0 == blooms_delete(key));

    return 0;
}

