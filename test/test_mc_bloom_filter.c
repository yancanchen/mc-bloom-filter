#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libmemcached/memcached.h"

int main(int argc,char** argv)
{
	memcached_st *memc;
	memcached_return rc;
	memcached_server_st *servers;
	
	memc = memcached_create(NULL);
	servers = memcached_server_list_append(NULL,"127.0.0.1",12345,&rc);
	rc = memcached_server_push(memc,servers);
	//memcached_server_free(servers);

	int conflict = 0;
	
	char *key = "hema";
	int key_len = strlen(key);
	char *val = "10000000|0.0001";
	char val_len = strlen(val);
	char subkey[1024];
	char getkey[1024];
	int subkey_len = 0;
	rc = memcached_add(memc,key,key_len,val,val_len,0,0);
	if(rc == MEMCACHED_SUCCESS){	
	
	}else{
		fprintf(stderr,"add success \r\n");
	}
	
	size_t value_len;
	unsigned int flag;
	
	int i;
	for(i = 0; i< 1000000;i++){
		sprintf(subkey,"%d",i);
		subkey_len = strlen(subkey);
		sprintf(getkey,"%s|%s",key,subkey);	
		//printf(".");
		
		char *res = memcached_get(memc,getkey,strlen(getkey),&value_len,&flag,&rc);
		if(rc == MEMCACHED_SUCCESS){	
			if(res != NULL && strcmp(res,"1") == 0){
				conflict ++;
				printf("get data :%s %d \n",res,value_len);
			}else{
				//printf("get data :%s %d \n",res,value_len);
			}
		}
		rc = memcached_set(memc,key,key_len,subkey,subkey_len,0,0);
		if(rc == MEMCACHED_SUCCESS){
			//printf("set key :%s data :%s success \n",key,subkey);
		}else{
			printf("set key :%s data :%s fail \n",key,subkey);
		}
	}
	fprintf(stderr,"conflict num %d",conflict);
	//memcached_delete(memc,key,strlen(key),0);
	memcached_free(memc);
}
