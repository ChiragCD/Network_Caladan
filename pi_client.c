#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

    if (argc < 3) {
        printf("Usage - <executable> <remote port> <num_terms>\n");
        return 0;
    }

    int remote_port = atoi(argv[1]);
    uint64_t num_terms = (uint64_t) strtoull(argv[2], NULL, 0);

    signal(SIGINT, handle_interrupt);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in self_addr, remote_addr;
    remote_addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", &remote_addr.sin_addr);
    remote_addr.sin_port = htons(remote_port);

    int buffer[BUF_LEN];
    buffer[0] = num_terms;
    int size = sendto(sock, buffer, BUF_LEN*sizeof(int), 0, (struct sockaddr *) &remote_addr, sizeof(remote_addr));

    int remote_addr_len = 0;
    size = recvfrom(sock, buffer, BUF_LEN*sizeof(int), 0, (struct sockaddr *) &remote_addr, &remote_addr_len);
    printf("Received %.20f\n", ((double *) buffer)[0]);
    terminate();
    return 0;
}
