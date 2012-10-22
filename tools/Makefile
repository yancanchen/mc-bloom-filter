
all: simulator.out


simulator.out : simulator.c ../bloom_filter.c ../murmur.c
	$(CC) -g -pg simulator.c ../bloom_filter.c ../murmur.c -o simulator.out -O2 -Wall -lm


clean:
	rm -f *.out

