#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <pwd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <event.h>
#include <assert.h>

#define HAVE_MALLOC_H
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "mc_bloom_filter.h"
#include "bloom_filter.h"
#include "daemon.h"

struct stats stats;
struct my_srv my_srv;
volatile unsigned int status = 0;
extern blooms_t *blooms;

void stats_init(void) {
    stats.curr_conns = stats.total_conns = stats.conn_structs = 0;
    stats.bytes_read = stats.bytes_written =  0;
    stats.add_count = stats.set_count = stats.get_count = 0;
    stats.started = time(0);
}

void stats_reset(void) {
    stats.total_conns = 0;
    stats.bytes_read = stats.bytes_written = 0;
    stats.add_count = stats.set_count = stats.get_count = 0;
}

void init_my_srv_default(void) {
    my_srv.maxconns = 1024;     /* to limit connections-related memory to about 5MB */
    my_srv.maxcore = 0;
    my_srv.port = 12345;
    my_srv.verbose = 0;
    my_srv.maxbytes = 1024*1024*1024;
    my_srv.lock_memory = 0;
    my_srv.username = 0;
    my_srv.pid_file = 0;
    my_srv.interface.s_addr = htonl(INADDR_ANY);
}

conn **freeconns;
int freetotal;
int freecurr;

void set_cork (conn *c, int val) {
    if (c->is_corked == val) return;
    c->is_corked = val;
#ifdef TCP_NOPUSH
    setsockopt(c->sfd, IPPROTO_TCP, TCP_NOPUSH, &val, sizeof(val));
#endif
}

void conn_init(void) {
    freetotal = 200;
    freecurr = 0;
    freeconns = (conn **)malloc(sizeof (conn *)*freetotal);
    return;
}

conn *conn_new(int sfd, int init_state, int event_flags) {
    conn *c;

    /* do we have a free conn structure from a previous close? */
    if (freecurr > 0) {
        c = freeconns[--freecurr];
    } else { /* allocate a new one */
        if (!(c = (conn *)malloc(sizeof(conn)))) {
            perror("malloc()");
            return 0;
        }
        c->rbuf = c->wbuf = 0;
        c->rbuf = (char *) malloc(DATA_BUFFER_SIZE);
        c->wbuf = (char *) malloc(DATA_BUFFER_SIZE);

        if (c->rbuf == 0 || c->wbuf == 0) {
            if (c->rbuf != 0) free(c->rbuf);
            if (c->wbuf != 0) free(c->wbuf);
            free(c);
            perror("malloc()");
            return 0;
        }
        c->rsize = c->wsize = DATA_BUFFER_SIZE;
        c->rcurr = c->rbuf;
        stats.conn_structs++;
    }

    if (my_srv.verbose > 1) {
        if (init_state == conn_listening)
            fprintf(stderr, "<%d server listening\n", sfd);
        else
            fprintf(stderr, "<%d new client connection\n", sfd);
    }

    c->sfd = sfd;
    c->state = init_state;
    c->rbytes = c->wbytes = 0;
    c->rlbytes = 0;
    c->wcurr = c->wbuf;
    c->write_and_go = conn_read;
    c->is_corked = 0;
    
    event_set(&c->event, sfd, event_flags, event_handler, (void *)c);
    c->ev_flags = event_flags;

    if (event_add(&c->event, 0) == -1) {
        if (freecurr < freetotal) {
            freeconns[freecurr++] = c;
        } else {
            free (c->rbuf);
            free (c->wbuf);
            free (c);
        }
        return 0;
    }

    stats.curr_conns++;
    stats.total_conns++;

    return c;
}

void conn_close(conn *c) {
    /* delete the event, the socket and the conn */
    event_del(&c->event);

    if (my_srv.verbose > 1)
        fprintf(stderr, "<%d connection closed.\n", c->sfd);

    close(c->sfd);
    /* if we have enough space in the free connections array, put the structure there */
    if (freecurr < freetotal) {
        freeconns[freecurr++] = c;
    } else {
        /* try to enlarge free connections array */
        conn **new_freeconns = realloc(freeconns, sizeof(conn *)*freetotal*2);
        if (new_freeconns) {
            freetotal *= 2;
            freeconns = new_freeconns;
            freeconns[freecurr++] = c;
        } else {
            free(c->rbuf);
            free(c->wbuf);
            free(c);
        }
    }

    stats.curr_conns--;

    return;
}

void out_string(conn *c, char *str) {
    int len;

    if (my_srv.verbose > 1)
        fprintf(stderr, ">%d %s\n", c->sfd, str);

    len = strlen(str);
    if (len + 2 > c->wsize) {
        /* ought to be always enough. just fail for simplicity */
        str = "SERVER_ERROR output line too long";
        len = strlen(str);
    }

    strcpy(c->wbuf, str);
    strcat(c->wbuf, "\r\n");
    c->wbytes = len + 2;
    c->wcurr = c->wbuf;

    c->state = conn_write;
    c->write_and_go = conn_read;
    return;
}

//reset the max memory size of the bloom filter
void process_setmem(conn *c,char *command){
    unsigned long msize;
    if(1 != sscanf(command + 6,"%lu",&msize) || msize > ((size_t)(-1)) >> 20){
        out_string(c, "CLIENT_ERROR bad command line like this,'setmem 1000' means max limit 1000m");    
        return ;
    }
    blooms->max_bytes = msize << 20;
    my_srv.maxbytes = blooms->max_bytes;
    out_string(c,"STORED");
    return;
}

//for the memory size calculate,won't malloc memory
void process_try(conn *c,char *command){
    unsigned long long n,m;
    int k;
    double tmp_e;
    
    char tmp[1024];
    char *pos = tmp;
    double e;
       if (2 != sscanf(command + 3,"%llu|%lf",&n,&e)) {
        out_string(c, "CLIENT_ERROR bad command line format,command like this 'try 100000|0.001'");    
        return;
    }
    size_t bytes = calculate(n,e,&m,&k,&tmp_e);
    if(bytes > 0 ){
        pos += sprintf(pos, "need_memory %lu(Bytes) ",(unsigned long)bytes);
        float bytes_m = (float)bytes/(1024*1024);
        if(bytes_m >= 1){
            pos += sprintf(pos, "%.3f(M) ",bytes_m);
        }
        float bytes_g = bytes_m/1024;
        
        if(bytes_g >= 1){
            pos += sprintf(pos, "%.3f(G) ",bytes_g);
        }
        pos += sprintf(pos, "\r\nuse_function_num %d \r\n",k);
        pos += sprintf(pos, "false_positive_rate  %lf\r\n",tmp_e);
        pos += sprintf(pos, "END");
        out_string(c,tmp);
    }else{
        out_string(c, "CALCULATE ERROR");
    }
    return;
    
}
/* 
 * we get here after reading the value in set/add/replace commands. The command
 * has been stored in c->item_comm, and the item is ready in c->item.
 */

void process_stat(conn *c, char *command) {
    time_t now = time(0);
    if (strcmp(command, "stats") == 0) {
        char temp[1024];
        pid_t pid = getpid();
        char *pos = temp;
        struct rusage usage;

        getrusage(RUSAGE_SELF, &usage);

        pos += sprintf(pos, "STAT pid %u\r\n", pid);
        pos += sprintf(pos, "STAT uptime %lu\r\n", now - stats.started);
        pos += sprintf(pos, "STAT time %ld\r\n", now);
        //bloom status
        pos += sprintf(pos, "STAT max_memory %lu\r\n", (unsigned long)blooms->max_bytes);
        pos += sprintf(pos, "STAT used_memory %ld\r\n", (unsigned long)blooms->bytes);
        pos += sprintf(pos, "STAT bloom_count %d\r\n", blooms->count);
        pos += sprintf(pos, "STAT bloom_power %d\r\n", blooms->power);

        pos += sprintf(pos, "STAT pointer_size %lu\r\n", (unsigned long)(8 * sizeof(void*)));
        pos += sprintf(pos, "STAT rusage_user %ld.%06ld\r\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
        pos += sprintf(pos, "STAT rusage_system %ld.%06ld\r\n", usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
        pos += sprintf(pos, "STAT curr_connections %u\r\n", stats.curr_conns - 1); /* ignore listening conn */
        pos += sprintf(pos, "STAT total_connections %u\r\n", stats.total_conns);
        pos += sprintf(pos, "STAT connection_structures %u\r\n", stats.conn_structs);
        pos += sprintf(pos, "STAT bytes_read %llu\r\n", stats.bytes_read);
        pos += sprintf(pos, "STAT bytes_written %llu\r\n", stats.bytes_written);
        pos += sprintf(pos, "END");
        out_string(c, temp);
        return;
    }
    
    //show all the blooms info    
    if(strcmp(command,"stats blooms") == 0){

        char *p = c->wbuf;
        char *end  = c->wbuf + c->wsize - 6;
        if(!list_empty(&blooms->list)) {
            bloom_t *bl;
            list_for_each_entry(bl, &blooms->list, list) {
                //32+1+21+2
                if(end - p <= 55) {
                    char *new_wbuf = realloc(c->wbuf, c->wsize*2);    
                    if(!new_wbuf){
                        fprintf(stderr, "Couldn't realloc input buffer\n");
                        out_string(c, "SERVER_ERROR out of memory");
                        return;
                    }
                    c->wbuf = new_wbuf;
                    c->wcurr = c->wbuf;
                    c->wsize *= 2;
                    p = c->wbuf + c->wbytes;
                    end  = c->wbuf + c->wsize - 6;
                }
                p += snprintf(p,end - p, "%s %lu\r\n", bl->key,(unsigned long)bl->bytes);
                c->wbytes = p - c->wbuf;
            }
        }
        p += sprintf(p, "END\r\n");
        c->wbytes = p - c->wbuf;
        c->wcurr = c->wbuf;
        c->state = conn_write;
        c->write_and_go = conn_read;
        
        return;
    }

    //stats bloom key
    if(strncmp(command,"stats bloom ",12) == 0){
        char key[MAX_KEY_LEN];
        int res;
        res = sscanf(command + 12, " %32s\n", key);
        if (res!=1 || strlen(key)==0 ) {
            out_string(c, "CLIENT_ERROR bad command line format,'stats bloom key'");    
            return;
        }

        char buff[1024];
        char *pos1 = buff;
        bloom_t *bloom = blooms_search(key);

        if(!bloom){
            pos1 += sprintf(pos1,"key %s not exists\r\n", key);
        }else{
            pos1 += sprintf(pos1, "STAT key %s\r\n", bloom->key);
            pos1 += sprintf(pos1, "STAT elements %llu\r\n", bloom->n);
            pos1 += sprintf(pos1, "STAT table_size %llu\r\n", bloom->m);
            pos1 += sprintf(pos1, "STAT table_size_bytes %lu\r\n", (unsigned long)bloom->bytes);
            pos1 += sprintf(pos1, "STAT false_positive_rate %f\r\n", bloom->e);
            pos1 += sprintf(pos1, "STAT hash_functions_count %d\r\n", bloom->k);
            pos1 += sprintf(pos1, "STAT get_count %llu\r\n", bloom->get_count);
            pos1 += sprintf(pos1, "STAT get_miss_count %llu\r\n", bloom->get_miss_count);
            pos1 += sprintf(pos1, "STAT set_count %llu\r\n", bloom->set_count);

        }
        pos1 += sprintf(pos1, "END");
        
        out_string(c,buff);    
        return;    
    }

    if (strcmp(command, "stats reset") == 0) {
        stats_reset();
        out_string(c, "RESET");
        return;
    }

    out_string(c, "ERROR");
}

/* 
 * we get here after reading the value in set/add/replace commands. The command
 * has been stored in c->item_comm, and the item is ready in c->item.
 */

void complete_nread(conn *c) {
    char *msg;
    int len;

    len = c->rlbytes - 2;
    msg = c->rcurr;

    if (strncmp(msg + len, "\r\n", 2) != 0) {
        out_string(c, "CLIENT_ERROR bad data chunk");
    } else {
        msg[len] = 0;
        if (c->item_comm == NREAD_SET){
            //set a bloom filter with key
            if (len && (len <= MAX_SUBKEY_LEN) && !strchr(msg,' ') && blooms_set(c->key,msg) == 0 ){
                out_string(c, "STORED");
            } else {
                out_string(c, "NOT_STORED");
            }
        }else if(c->item_comm == NREAD_ADD){
            unsigned long long n;
            double e;
            if ((2 != sscanf(msg,"%llu|%lf",&n,&e)) || !n || e<=0 || e>=1) {
                out_string(c, "NOT_STORED");
                return;
            }

            if(!strchr(c->key,'|') && blooms_add(c->key,n,e) == 0){    
                out_string(c, "STORED");
            } else {
                out_string(c, "NOT_STORED");
            }

        }else{
            out_string(c, "CLIENT_ERROR unsuport type");
        }
    }
}

void process_command(conn *c, char *command) {

    /* 
     * for commands set/add/replace, we build an item and read the data
     * directly into it, then continue in nread_complete().
     */ 

    if (my_srv.verbose > 1)
        fprintf(stderr, "<%d %s\n", c->sfd, command);

    /* All incoming commands will require a response, so we cork at the beginning,
       and uncork at the very end (usually by means of out_string)  */
    set_cork(c, 1);
    /* the function get bloom filter by key ,format: name_key*/
    if (strncmp(command, "get ", 4) == 0) {
        char key[256];
        char *subkey;
        char *start = command + 4;
        char *p = c->wbuf;
        char *end = c->wbuf + c->wsize - 6;
        int next;
        int res;

        while(sscanf(start, " %250s%n",key,&next) >= 1){    
            start += next;
             
            if((subkey = strchr(key, '|'))) {
                *subkey = '\0';
                subkey ++;
                res = blooms_get(key,subkey);
                if (res == 1) {
                    p += snprintf(p,end - p , "VALUE %s|%s 0 1\r\n%d\r\n", key,subkey,res); 
                }
            }
        }
        p += sprintf(p, "END\r\n");
        c->wbytes = p - c->wbuf;
        c->wcurr = c->wbuf;

        c->state = conn_write;
        c->write_and_go = conn_read;
        return;
    }

    //add create bloom filter 
    //add name 0 0 10
    //10000|0.0001 bit_chongtulv
    //set insert a new bloom_filter key
    if ((strncmp(command, "add ", 4) == 0 && (c->item_comm = NREAD_ADD)) ||
        (strncmp(command, "set ", 4) == 0 && (c->item_comm = NREAD_SET))) {
        char key[251];
        int flags;
        time_t expire;
        int len, res;

        res = sscanf(command, "%*s %250s %u %ld %d\n", key, &flags, &expire, &len);
        if (res!=4 || strlen(key)==0 || len < 0) {
            out_string(c, "CLIENT_ERROR bad command line format");
            return;
        }
        if (strlen(key) >= MAX_KEY_LEN) {
            // swallow the data line
            out_string(c, "NOT_STORED");
            c->write_and_go = conn_swallow;
            c->sbytes = len+2;
            return;
        }
        strcpy(c->key,key); 

        c->rlbytes = len+2;
        c->state = conn_nread;
        return;
    }

    //del key and free the bloom filter
    if (strncmp(command, "delete ", 7) == 0) {
        char key[MAX_KEY_LEN];
        char *start = command + 7;
        char *p = c->wbuf;
        
        if(sscanf(start, " %31s", key) >= 1) {
            if(blooms_delete(key) == 0){
                p += sprintf(p, "DELETED\r\n");
            }else{
                p += sprintf(p,"NOT_FOUND\r\n");    
            }
        }
        c->wbytes = p - c->wbuf;
        c->wcurr = c->wbuf;
        c->state = conn_write;
        c->write_and_go = conn_read;
        return;
    }

    if (strncmp(command, "stats", 5) == 0) {
        process_stat(c, command);
        return;
    }
    
    //calculate of the memory size,won't malloc memory
    if(strncmp(command, "try", 3) == 0){
        process_try(c,command);
        return;
    }

    //set the max memory limit
    //you can enlarge the max memory limit without restart service
    if(strncmp(command,"setmem",6) == 0){
        process_setmem(c,command);
        return;
    }

    if (strcmp(command, "quit") == 0) {
        c->state = conn_closing;
        return;
    }

    out_string(c, "ERROR");
    return;
}

/* 
 * if we have a complete line in the buffer, process it.
 */
int try_read_command(conn *c) {
    char *el, *cont;

    if (!c->rbytes)
        return 0;
    el = memchr(c->rcurr, '\n', c->rbytes);
    if (!el)
        return 0;
    cont = el + 1;
    if (el - c->rcurr > 1 && *(el - 1) == '\r') {
        el--;
    }
    *el = '\0';

    process_command(c, c->rcurr);

    c->rbytes -= (cont - c->rcurr);
    c->rcurr = cont;

    return 1;
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close. 
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 * return 0 if there's nothing to read on the first read.
 */
int try_read_network(conn *c) {
    int gotdata = 0;
    int res;

    if (c->rcurr != c->rbuf) {
        if (c->rbytes != 0) /* otherwise there's nothing to copy */
            memmove(c->rbuf, c->rcurr, c->rbytes);
        c->rcurr = c->rbuf;
    }

    while (1) {
        if (c->rbytes >= c->rsize) {
            char *new_rbuf = realloc(c->rbuf, c->rsize*2);
            if (!new_rbuf) {
                if (my_srv.verbose > 0)
                    fprintf(stderr, "Couldn't realloc input buffer\n");
                c->rbytes = 0; /* ignore what we read */
                out_string(c, "SERVER_ERROR out of memory");
                c->write_and_go = conn_closing;
                return 1;
            }
            c->rcurr  = c->rbuf = new_rbuf;
            c->rsize *= 2;
        }
        res = read(c->sfd, c->rbuf + c->rbytes, c->rsize - c->rbytes);
        if (res > 0) {
            stats.bytes_read += res;
            gotdata = 1;
            c->rbytes += res;
            continue;
        }
        if (res == 0) {
            /* connection closed */
            c->state = conn_closing;
            return 1;
        }
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else return 0;
        }
    }
    return gotdata;
}

int update_event(conn *c, int new_flags) {
    if (c->ev_flags == new_flags)
        return 1;
    if (event_del(&c->event) == -1) return 0;
    event_set(&c->event, c->sfd, new_flags, event_handler, (void *)c);
    c->ev_flags = new_flags;
    if (event_add(&c->event, 0) == -1) return 0;
    return 1;
}

void drive_machine(conn *c) {

    int exit = 0;
    int sfd, flags = 1;
    socklen_t addrlen;
    struct sockaddr_in addr;
    conn *newc;
    int res;

    while (!exit) {
        /* printf("state %d\n", c->state);*/
        switch(c->state) {
            case conn_listening:
                addrlen = sizeof(addr);
                if ((sfd = accept(c->sfd, (struct sockaddr*)(&addr), &addrlen)) == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        exit = 1;
                        break;
                    } else {
                        perror("accept()");
                        exit = 1;
                        break;
                    }
                    break;
                }
                if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
                        fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    perror("setting O_NONBLOCK");
                    close(sfd);
                    break;
                }        
                newc = conn_new(sfd, conn_read, EV_READ | EV_PERSIST);
                if (!newc) {
                    if (my_srv.verbose > 0)
                        fprintf(stderr, "couldn't create new connection\n");
                    close(sfd);
                    break;
                }

                break;

            case conn_read:
                if (try_read_command(c)) {
                    continue;
                }
                if (try_read_network(c)) {
                    continue;
                }
                /* we have no command line and no data to read from network */
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    if (my_srv.verbose > 0)
                        fprintf(stderr, "Couldn't update event\n");
                    c->state = conn_closing;
                    break;
                }
                exit = 1;
                break;

            case conn_nread:
                /* we are reading rlbytes into ritem; */
                if (c->rbytes >= c->rlbytes) {
                    complete_nread(c);
                    c->rcurr += c->rlbytes;
                    c->rbytes -= c->rlbytes;
                    c->rlbytes = 0;
                    continue;
                }

                if (try_read_network(c)) {
                    continue;
                }

                /* we have no command line and no data to read from network */
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    if (my_srv.verbose > 0)
                        fprintf(stderr, "Couldn't update event\n");
                    c->state = conn_closing;
                    break;
                }
                exit = 1;
                break;

            case conn_swallow:
                /* we are reading sbytes and throwing them away */
                if (c->sbytes == 0) {
                    c->state = conn_read;
                    break;
                }

                /* first check if we have leftovers in the conn_read buffer */
                if (c->rbytes > 0) {
                    int tocopy = c->rbytes > c->sbytes ? c->sbytes : c->rbytes;
                    c->sbytes -= tocopy;
                    c->rcurr += tocopy;
                    c->rbytes -= tocopy;
                    break;
                }

                /*  now try reading from the socket */
                res = read(c->sfd, c->rbuf, c->rsize > c->sbytes ? c->sbytes : c->rsize);
                if (res > 0) {
                    stats.bytes_read += res;
                    c->sbytes -= res;
                    break;
                }
                if (res == 0) { /* end of stream */
                    c->state = conn_closing;
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (!update_event(c, EV_READ | EV_PERSIST)) {
                        if (my_srv.verbose > 0)
                            fprintf(stderr, "Couldn't update event\n");
                        c->state = conn_closing;
                        break;
                    }
                    exit = 1;
                    break;
                }
                /* otherwise we have a real error, on which we close the connection */
                if (my_srv.verbose > 0)
                    fprintf(stderr, "Failed to read, and not due to blocking\n");
                c->state = conn_closing;
                break;

            case conn_write:
                /* we are writing wbytes bytes starting from wcurr */
                if (c->wbytes == 0) {
                    c->state = c->write_and_go;
                    if (c->state == conn_read)
                        set_cork(c, 0);
                    break;
                }
                res = write(c->sfd, c->wcurr, c->wbytes);
                if (res > 0) {
                    stats.bytes_written += res;
                    c->wcurr  += res;
                    c->wbytes -= res;
                    break;
                }
                if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (!update_event(c, EV_WRITE | EV_PERSIST)) {
                        if (my_srv.verbose > 0)
                            fprintf(stderr, "Couldn't update event\n");
                        c->state = conn_closing;
                        break;
                    }        
                    exit = 1;
                    break;
                }
                /* if res==0 or res==-1 and error is not EAGAIN or EWOULDBLOCK,
                   we have a real error, on which we close the connection */
                if (my_srv.verbose > 0)
                    fprintf(stderr, "Failed to write, and not due to blocking\n");
                c->state = conn_closing;
                break;

            case conn_closing:
                conn_close(c);
                exit = 1;
                break;
        }

    }

    return;
}


void event_handler(int fd, short which, void *arg) {
    conn *c;

    c = (conn *)arg;
    c->which = which;

    /* sanity */
    if (fd != c->sfd) {
        if (my_srv.verbose > 0)
            fprintf(stderr, "Catastrophic: event fd doesn't match conn fd!\n");
        conn_close(c);
        return;
    }

    /* do as much I/O as possible until we block */
    drive_machine(c);

    /* wait for next event */
    return;
}

int new_socket(void) {
    int sfd;
    int flags;

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket()");
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
            fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("setting O_NONBLOCK");
        close(sfd);
        return -1;
    }
    return sfd;
}

int server_socket(int port) {
    int sfd;
    struct linger ling = {0, 0};
    struct sockaddr_in addr;
    int flags =1;

    if ((sfd = new_socket()) == -1) {
        return -1;
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
#if !defined(TCP_NOPUSH)
    setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
#endif

    /* 
     * the memset call clears nonstandard fields in some impementations
     * that otherwise mess things up.
     */
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = my_srv.interface;
    if (bind(sfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind()");
        close(sfd);
        return -1;
    }
    if (listen(sfd, 1024) == -1) {
        perror("listen()");
        close(sfd);
        return -1;
    }
    return sfd;
}

/* invoke right before gdb is called, on assert */
void pre_gdb () {
    int i = 0;
    if(l_socket) close(l_socket);
    for (i=3; i<=500; i++) close(i); /* so lame */
    kill(getpid(), SIGABRT);
}

void save_pid(pid_t pid,char *pid_file) {
    FILE *fp;
    if (!pid_file)
        return;

    if (!(fp = fopen(pid_file,"w"))) {
        fprintf(stderr,"Could not open the pid file %s for writing\n",pid_file);
        return;
    }

    fprintf(fp,"%ld\n",(long) pid);
    if (fclose(fp) == -1) {
        fprintf(stderr,"Could not close the pid file %s.\n",pid_file);
        return;
    }
}

void remove_pidfile(char *pid_file) {
    if (!pid_file)
        return;

    if (unlink(pid_file)) {
        fprintf(stderr,"Could not remove the pid file %s.\n",pid_file);
    }

}

int l_socket=0;
void usage(){
    printf("PACKAGE %s VERSION %s \n",PACKAGE_NAME,PACKAGE_VERSION);
    printf("-p <num>      port number to listen on,default is 12345\n");
    printf("-l <ip_addr>  interface to listen on, default is INDRR_ANY\n");
    printf("-d            run as a daemon\n");
    //printf("-r            maximize core file limit\n");
    printf("-u <username> assume identity of <username> (only when run as root)\n");
    //printf("-m <num>      max memory to use for items in megabytes, default is 64 MB\n");
    printf("-c <num>      max simultaneous connections, default is 1024\n");
    printf("-v            verbose (print errors/warnings while in event loop)\n");
    printf("-vv           very verbose (also print client commands/reponses)\n");
    printf("-h            print this help and exit\n");
    printf("-i            print memcached and libevent license\n");
    printf("-P <file>     save PID in <file>, only used with -d option\n");
    return;
}

void usage_license(){
    printf("SOFTNAME %s\r\n",PACKAGE_NAME);
    printf("VERSION %s\r\n",PACKAGE_VERSION);
    return;
}

int main(int argc, char *argv[]) 
{
    int c;
    conn *l_conn;
    struct in_addr addr;
    int daemonize = 1;
    struct passwd *pw;
    struct sigaction sa;
    struct rlimit rlim;

    /* init my_srv */
    init_my_srv_default();

    /* set stderr non-buffering (for running under, say, daemontools) */
    setbuf(stderr, NULL);

    /* process arguments */
    while ((c = getopt(argc, argv, "p:c:khirvdl:u:P:m:")) != -1) {
        switch (c) {
        case 'p':
            my_srv.port = atoi(optarg);
            break;
        // max memory now not use 
        case 'm':
            my_srv.maxbytes = ((size_t)atoi(optarg))*1024*1024;
            break;
        case 'c':
            my_srv.maxconns = atoi(optarg);
            break;
        case 'h':
            usage();
            exit(0);
        case 'i':
            usage_license();
            exit(0);
        case 'k':
            my_srv.lock_memory = 1;
            break;
        case 'v':
            my_srv.verbose++;
            break;
        case 'l':
            if (!inet_pton(AF_INET, optarg, &addr)) {
                fprintf(stderr, "Illegal address: %s\n", optarg);
                return 1;
            } else {
                my_srv.interface = addr;
            }
            break;
        case 'd':
            daemonize = 1;
            break;
        case 'r':
            my_srv.maxcore = 1;
            break;
        case 'u':
            my_srv.username = optarg;
            break;
        case 'P':
            my_srv.pid_file = optarg;
            break;
        default:
            fprintf(stderr, "Illegal argument \"%c\"\n", c);
            return 1;
        }
    }

    if (my_srv.maxcore) {
        struct rlimit rlim_new;
        /* 
         * First try raising to infinity; if that fails, try bringing
         * the soft limit to the hard. 
         */
        if (getrlimit(RLIMIT_CORE, &rlim)==0) {
            rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
            if (setrlimit(RLIMIT_CORE, &rlim_new)!=0) {
                /* failed. try raising just to the old max */
                rlim_new.rlim_cur = rlim_new.rlim_max = 
                    rlim.rlim_max;
                (void) setrlimit(RLIMIT_CORE, &rlim_new);
            }
        }
        /* 
         * getrlimit again to see what we ended up with. Only fail if 
         * the soft limit ends up 0, because then no core files will be 
         * created at all.
         */

        if ((getrlimit(RLIMIT_CORE, &rlim)!=0) || rlim.rlim_cur==0) {
            fprintf(stderr, "failed to ensure corefile creation\n");
            exit(1);
        }
    }

    /* 
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */

    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "failed to getrlimit number of files\n");
        exit(1);
    } else {
        int maxfiles = my_srv.maxconns;
        if (rlim.rlim_cur < maxfiles) 
            rlim.rlim_cur = maxfiles + 3;
        if (rlim.rlim_max < rlim.rlim_cur)
            rlim.rlim_max = rlim.rlim_cur;
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            fprintf(stderr, "failed to set rlimit for open files. "
                    "Try running as root or requesting smaller maxconns value.\n");
            exit(1);
        }
    }

    /* 
     * initialization order: first create the listening socket
     * (may need root on low ports), then drop root if needed,
     * then daemonise if needed, then init libevent (in some cases
     * descriptors created by libevent wouldn't survive forking).
     */

    /* create the listening socket and bind it */
    l_socket = server_socket(my_srv.port);
    if (l_socket == -1) {
        fprintf(stderr, "failed to listen\n");
        exit(1);
    }

    /* lose root privileges if we have them */
    if (getuid()== 0 || geteuid()==0) {
        if (my_srv.username==0 || *(my_srv.username)=='\0') {
            fprintf(stderr, "can't run as root without the username switch\n");
            return 1;
        }
        if ((pw = getpwnam(my_srv.username)) == 0) {
            fprintf(stderr, "can't find the user %s to switch to\n", my_srv.username);
            return 1;
        }
        if (setgid(pw->pw_gid)<0 || setuid(pw->pw_uid)<0) {
            fprintf(stderr, "failed to assume identity of user %s\n", my_srv.username);
            return 1;
        }
    }

    stats_init();

    /* daemonize if requested */
    /* if we want to ensure our ability to dump core, don't chdir to / */
    if (daemonize) {
        int res;
        res = daemon(my_srv.maxcore, my_srv.verbose);
        if (res == -1) {
            fprintf(stderr, "failed to daemon() in order to daemonize\n");
            return 1;
        }
    }


    /* initialize other stuff */
    event_init();
    conn_init();
            
    if (blooms_init(my_srv.maxbytes)) {
        fprintf(stderr, "blooms init failed, No enough memory?");
        exit(-1);
    }

    /* lock paged memory if needed */
    if (my_srv.lock_memory) {
#define HAVE_MLOCKALL
#ifdef HAVE_MLOCKALL
        mlockall(MCL_CURRENT | MCL_FUTURE);
#else
        fprintf(stderr, "warning: mlockall() not supported on this platform.  proceeding without.\n");
#endif
    }

    /*
     * ignore SIGPIPE signals; we can use errno==EPIPE if we
     * need that information
     */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 ||
            sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        exit(1); 
    }

    /* create the initial listening connection */
    if (!(l_conn = conn_new(l_socket, conn_listening, EV_READ | EV_PERSIST))) {
        fprintf(stderr, "failed to create listening connection");
        exit(1);
    }

    /* save the PID in if we're a daemon */
    if (daemonize)
        save_pid(getpid(),my_srv.pid_file);

    fprintf(stderr, "service started!\r\n");

    /* enter the loop */
    event_loop(0);

    /* remove the PID file if we're a daemon */
    if (daemonize)
        remove_pidfile(my_srv.pid_file);

    return 0;
}

