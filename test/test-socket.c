#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <unistd.h> 
#include <arpa/inet.h>
#include <assert.h>

#define DEST_IP "127.0.0.1"
#define DEST_PORT 12345

int sockfd;
struct sockaddr_in dest_addr;
char sendbuf[256] = "";
char recvbuf[2048] = "";

int sendwork (char *msg)
{
    strcpy(sendbuf, msg);
    strcat(sendbuf, "\n");
    printf("%.255s",sendbuf);
    int len;
    len = strlen(sendbuf);
    int re = send(sockfd, sendbuf, len, 0);
    if(-1 == re) {perror("send fail");return 1;}
    return 0;
}

int recvwork ()
{
    int re = recv(sockfd, recvbuf, 2048, 0 );
    if(-1 == re) {perror("recv fail");return 1;}
    recvbuf[re] = '\0';
    printf( "Bytes Recv: %d\n", re);
    printf("%.2047s\r\n", recvbuf);    
    return 0;
}


int main(int argc, char *argv[])
{

    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    dest_addr.sin_family = AF_INET; 
    dest_addr.sin_port = htons(DEST_PORT); 
    dest_addr.sin_addr.s_addr = inet_addr(DEST_IP);
    bzero(&(dest_addr.sin_zero),8); /* zero the rest of the struct */

    struct timeval timeout = {1,0}; 
    setsockopt(sockfd, SOL_SOCKET,SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval));
    setsockopt(sockfd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval));

    int re = connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr));
    if(-1 == re)  perror("connect fail");

    if(argc != 1){
        assert(!sendwork(argv[1]));
        assert(!recvwork());
    }else{
        char buf[256] = "";
        while (fgets(buf, 256, stdin)) {
            while (buf[strlen(buf) - 1] == '\n') buf[strlen(buf) - 1] = '\0';
            while (buf[strlen(buf) - 1] == '\r') buf[strlen(buf) - 1] = '\0';
        
            assert(!sendwork(buf));
            assert(!recvwork());
        }
 
    }

//printf("press anykey to close connect");
getchar();
close(sockfd);
return 0;
}
