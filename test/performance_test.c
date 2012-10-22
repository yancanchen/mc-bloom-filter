#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <libmemcached/memcached.h>

#define MAX_THREAD_NUM 1010
#define MAX_REQUEST_NUM 268435455
pthread_t threads[MAX_THREAD_NUM];
int thread_num;
int request_num;

char *emaoe; //expected_max_amount_of_elements
char *fpr; // false_positive_rate
char *target;

volatile unsigned long total_useconds;

#define CHECK_LEVEL 20
unsigned long final_res[CHECK_LEVEL];
unsigned long tmout;

long tvsub(struct timeval *etv, struct timeval *stv)
{
    long sec = etv->tv_sec - stv->tv_sec;
    long usec = etv->tv_usec - stv->tv_usec;
    return (sec * 1000000 + usec);
}

int check_table[CHECK_LEVEL] = {
        50, 100, 200, 300, 400, 500, 600, 700, 800, 900,
        1000, 3000, 5000, 6000, 7000, 8000, 9000, 10000, 30000, 50000};

void update_usc(long usc)
{
    int i;
    for (i = 0; i < CHECK_LEVEL; i ++) {
        if (usc <= check_table[i]) {
            __sync_fetch_and_add(&final_res[i], 1);
            return;
        }
    }
    
    __sync_fetch_and_add(&tmout, 1);
    return;
}

void print_usc()
{
    int i;
    for (i = 0; i < CHECK_LEVEL; i ++) {
        fprintf(stderr, "usec:%u cnt:%lu\r\n", check_table[i], final_res[i]);
    }

    fprintf(stderr, "TMOUT:%lu\r\n", tmout);
}

void request_init(void **req, char *srvs)
{
    memcached_st *mc;
    memcached_server_st *list;
    int err;

    list = memcached_servers_parse(srvs);
    if (!list) {
        fprintf(stderr, "bad memcached target:\r\n");
        exit(1);
    }

    mc = memcached_create(0);
    if (!mc) {
        fprintf(stderr, "mc create failed\r\n");
        exit(1);
    }

    if (MEMCACHED_SUCCESS != (err = memcached_server_push(mc, list))) {
        fprintf(stderr, "init memcached failed[%s]", memcached_strerror(mc, err));
        exit(1);
    }
    *req = mc;
}

void do_request(void *req, int num, struct timeval *stv, struct timeval *etv)
{
    memcached_st *memc = (memcached_st *)req;
    memcached_return_t rc;

#ifdef TEST_GET
    char key_buf[1024];
    int key_len = sprintf(key_buf, "%s|%d", emaoe, num);
    unsigned int flag = 0;
    unsigned long value_len = 0;

    gettimeofday(stv, 0);
    memcached_get(memc, key_buf, key_len, &value_len, &flag, &rc);
    gettimeofday(etv, 0);

    if(rc != MEMCACHED_SUCCESS){    
        fprintf(stderr,"%d get key :%s fail %s\n",num, key_buf, memcached_strerror(memc, rc));
    }
#endif

#ifdef TEST_SET
    char subkey[32];
    int subkeylen = sprintf(subkey, "%u", num);
    int keylen = strlen(emaoe);

    gettimeofday(stv, 0);
    rc = memcached_set(memc, emaoe, keylen, subkey, subkeylen, 0, 0);
    gettimeofday(etv, 0);

    if(rc != MEMCACHED_SUCCESS){
        fprintf(stderr,"%d set key :0 data :%s fail %s\n",num, subkey, memcached_strerror(memc, rc));
    }
#endif

}

void *press_thread(void *arg)
{
    struct timeval stv, etv;
    long useconds = 0;
    long usc;
    long me = (long)arg;
    int i;

    void *req;

    request_init(&req, target);

    for (i = me; i < request_num; i += thread_num) {
        do_request(req, i, &stv, &etv);
        usc = tvsub(&etv, &stv);
        useconds += usc;
        update_usc(usc);
    }

    __sync_fetch_and_add(&total_useconds, useconds);

    return 0;
}

int main(int argc, char *argv[])
{
    thread_num = atoi(argv[1]);
    request_num = atoi(argv[2]);
    emaoe = argv[3];
    fpr = argv[4];

    target = argv[5];

    if (thread_num > MAX_THREAD_NUM) thread_num = MAX_THREAD_NUM;
    if (request_num > MAX_REQUEST_NUM) request_num = MAX_REQUEST_NUM;

    struct timeval stv, etv;
    unsigned long i;


#ifdef TEST_SET
    {
        void *req;
        request_init(&req, target);
        char val[1024];
        sprintf(val, "%s|%s", emaoe, fpr);
        memcached_delete(req, emaoe, strlen(emaoe), 0);
        memcached_return_t rc = memcached_add(req, emaoe, strlen(emaoe), val, strlen(val), 0, 0);
        if (rc != MEMCACHED_SUCCESS) {
            fprintf(stderr,"add key %s: val:%s fail %s\n",emaoe, val, memcached_strerror(req, rc));
            exit(-1);
        }
        memcached_free(req);
    }
#endif



    gettimeofday(&stv, 0);
    for (i = 0; i < thread_num; i ++) {
        int ret;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        
        if ((ret = pthread_create(&threads[i], &attr, press_thread, (void *)i)) != 0) {
            fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
            exit(1);
        }
    }

    for (i = 0; i < thread_num; i ++) {
        pthread_join(threads[i], 0);
    }
    gettimeofday(&etv, 0);

    fprintf(stdout, "Thread num: %d, Total request: %d, E.M.A.O.E.: %s, F.P.R.: %s\r\n", 
        thread_num, request_num, emaoe, fpr);

    fprintf(stdout, "Total request time:%lu, Average request time:%.3lf (microseconds)\r\n", 
        total_useconds, (double)total_useconds / request_num);

    fprintf(stdout, "All request finished in %.3lf seconds, speed: %.3lf req/s\r\n", 
        (double)tvsub(&etv, &stv)/1000000, (double)request_num/((double)tvsub(&etv, &stv)/1000000));

    //print_usc();

    return 0;
}
