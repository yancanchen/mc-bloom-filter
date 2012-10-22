SRCS:=$(wildcard *.c)
OBJS:=$(SRCS:.c=.o)

MYSRV_SRC:= mc_bloom_filter.c daemon.c  bloom_filter.c murmur.c 

MYSRV_OBJ:=$(MYSRV_SRC:.c=.o)

all: mc_bloom_filter mc_bloom_filter_
	rm -fr *.o

mtools:
	$(MAKE) -C tools

mc_bloom_filter : $(MYSRV_OBJ)
	cc -g -o $@ $(MYSRV_OBJ) -lm -levent -L/usr/local/libevent/lib/

mc_bloom_filter_ : $(MYSRV_OBJ)
	cc -g -o $@ $(MYSRV_OBJ) -D_DEBUG -lm -levent -L/usr/local/libevent/lib/

$(OBJS): %.o: %.c
	$(CC) -g -c $< -O2 -Wall -I/usr/local/libevent/include 

clean:
	rm -fr mc_bloom_filter_ mc_bloom_filter *.o
