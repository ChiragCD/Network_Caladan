#include <signal.h>                                                                                                                                           
#include <stdio.h>                                                                                                                                            
#include <stdlib.h>                                                                                                                                           
#include <stdint.h>                                                                                                                                           
#include <unistd.h>                                                                                                                                           
#include <sys/socket.h>                                                                                                                                       
#include <arpa/inet.h>   
#include <time.h>                                                                                                                                     
                                                                                                                                                              
#define BUF_LEN 100                                                                                                                                           
                                                                                                                                                              
int sock;

void terminate() {
    int true = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int));
    close(sock);
    exit(0);
}

void handle_interrupt(int signal) {
    terminate();
}

int main(int argc, char * argv[]) {

    if (argc < 2) {
        printf("Usage - <executable> <num_terms>\n");
        return 0;
    }

    uint64_t num_terms = (uint64_t) strtoull(argv[1], NULL, 0);

    signal(SIGINT, handle_interrupt);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in self_addr, remote_addr;
    remote_addr.sin_family = AF_INET;
    inet_aton("10.10.1.2", &remote_addr.sin_addr);
    remote_addr.sin_port = htons(8000); 

    uint64_t buffer[BUF_LEN];
    int priority = 5;
    buffer[0] = num_terms;
    buffer[3] = priority;
    struct timeval t1, t2;
    gettimeofday(&t1,NULL);
    int size = sendto(sock, buffer, BUF_LEN*sizeof(uint64_t), 0, (struct sockaddr *) &remote_addr, sizeof(remote_addr));

    int remote_addr_len = 0;
    size = recvfrom(sock, buffer, BUF_LEN*sizeof(uint64_t), 0, (struct sockaddr *) &remote_addr, &remote_addr_len);
    gettimeofday(&t2,NULL);

    unsigned long time1 = t1.tv_sec * 1000000 + t1.tv_usec;
    unsigned long time2 = t2.tv_sec * 1000000 + t2.tv_usec;
    printf("%d %lu %lu\n", priority, time1, time2);
    terminate();
    return 0;
}