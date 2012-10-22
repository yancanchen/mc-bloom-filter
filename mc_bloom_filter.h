#define DATA_BUFFER_SIZE 2048

#if defined(TCP_CORK) && !defined(TCP_NOPUSH)
#define TCP_NOPUSH TCP_CORK
#endif

#define PACKAGE_STRING "mc_bloom_filter 0.1.1"  
#define PACKAGE_NAME "mc_bloom_filter"
#define PACKAGE_VERSION "0.1.1" 

#define MAX_GETKEY_LEN 250
#define MAX_KEY_LEN 32
#define MAX_SUBKEY_LEN 217
#define NREAD_ADD 1
#define NREAD_SET 2   

struct stats {
    unsigned int  curr_conns;
    unsigned int  total_conns;
    unsigned int  conn_structs;
    unsigned long long get_count;
    unsigned long long set_count;
    unsigned long long add_count;
    time_t        started;          /* when the process was started */
    unsigned long long bytes_read;
    unsigned long long bytes_written;
};

struct my_srv {
    int maxconns;
    int maxcore;
    int port;
    int verbose;
    int lock_memory;
	size_t maxbytes;
    char *username;
    char *pid_file;
    struct in_addr interface;
};

extern struct stats stats;
extern struct my_srv my_srv;

enum conn_states {
    conn_listening,  /* the socket which listens for connections */
    conn_read,       /* reading in a command line */
    conn_write,      /* writing out a simple response */
    conn_nread,      /* reading in a fixed number of bytes */
    conn_swallow,    /* swallowing unnecessary bytes w/o storing */
    conn_closing     /* closing this connection */
};

typedef struct {
    int    sfd;
    int    state;
    struct event event;
    short  ev_flags;
    short  which;   /* which events were just triggered */

    char   *rbuf;   /* buffer to read commands into */
    char   *rcurr;  /* but if we parsed some already, this is where we stopped */
    int    rsize;   /* total allocated size of rbuf */
    int    rbytes;  /* how much data, starting from rcur, do we have unparsed */
    int    rlbytes;

    char   *wbuf;
    char   *wcurr;
    int    wsize;
    int    wbytes; 
    int    write_and_go; /* which state to go into after finishing current write */
    char   is_corked;         /* boolean, connection is corked */
    char   item_comm; /* which one is it: set/add */ 
    char   r1;
    char   r2;
    char   key[MAX_KEY_LEN]; /* the key for add set */

    /* data for the swallow state */
    int    sbytes;    /* how many bytes to swallow */
} conn;

/* listening socket */
extern int l_socket;

/* temporary hack */
/* #define assert(x) if(!(x)) { printf("assert failure: %s\n", #x); pre_gdb(); }
   void pre_gdb (); */

/*
 * Functions
 */

/* 
 * given time value that's either unix time or delta from current unix time, return
 * unix time. Use the fact that delta can't exceed one month (and real time value can't 
 * be that low).
 */

    
/* event handling, network IO */
void event_handler(int fd, short which, void *arg);
conn *conn_new(int sfd, int init_state, int event_flags);
void conn_close(conn *c);
void conn_init(void);
void drive_machine(conn *c);
int new_socket(void);
int server_socket(int port);
int update_event(conn *c, int new_flags);
int try_read_command(conn *c);
int try_read_network(conn *c);
void complete_nread(conn *c);
void process_command(conn *c, char *command);

/* stats */
void stats_reset(void);
void stats_init(void);

void usage();
void usage_license();

