#define _DEBUG
#include "../bloom_filter.h"

#define N 1000000000
#define E .0001
#define KEY "test"

int main (int argc, char *argv[]) 
{
    blooms_init(1000000000000000);
	printf("EXAMPLE:\r\n");
	blooms_add(KEY, N, E);
	bloom_status(KEY);
	bloom_status("test3");
	blooms_status();
	blooms_set(KEY, "asd");
	blooms_get(KEY, "asd");
	blooms_get(KEY, "ppp");
	blooms_status();
	blooms_delete(KEY);
	blooms_delete("test2");
	blooms_status();

	char str[25];
    int i;
    for(i=0; i<10; i++){
        sprintf(str, "%d ",i); 
		blooms_add(str, N, E);
		blooms_status();
    }

	
	return 0;
}

