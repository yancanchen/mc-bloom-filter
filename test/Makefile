
all: test_bloom_filter.out get_performance_test.out set_performance_test.out

get_performance_test.out : performance_test.c
	$(CC) -g -Wall performance_test.c -o get_performance_test.out -lmemcached -DTEST_GET

set_performance_test.out : performance_test.c
	$(CC) -g -Wall performance_test.c -o set_performance_test.out -lmemcached -DTEST_SET

test_bloom_filter.out : test_bloom_filter.c ../bloom_filter.c ../murmur.c
	$(CC) -g -Wall -pg test_bloom_filter.c ../bloom_filter.c ../murmur.c -o test_bloom_filter.out -O2 -Wall -lm

clean:
	rm -f *.out

