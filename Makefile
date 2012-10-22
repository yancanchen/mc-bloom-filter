SRCS:=$(wildcard *.c)
OBJS:=$(SRCS:.c=.o)

all: mc_bloom_filter mc_bloom_filter_
	rm -fr *.o

mtools:
	$(MAKE) -C tools

mc_bloom_filter : $(OBJS)
	cc -g -o $@ $(OBJS) -lm -levent -L/usr/local/libevent/lib/

mc_bloom_filter_ : $(OBJS)
	cc -g -o $@ $(OBJS) -D_DEBUG -lm -levent -L/usr/local/libevent/lib/

$(OBJS): %.o: %.c
	$(CC) -g -c $< -O2 -Wall -I/usr/local/libevent/include 

clean:
	rm -fr mc_bloom_filter_ mc_bloom_filter *.o
