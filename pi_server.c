#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUF_LEN 100
#define THREAD_COUNT 100

int sock;
int buffers[THREAD_COUNT][BUF_LEN];
pthread_t threads[THREAD_COUNT];
struct sockaddr_in remote_addrs[THREAD_COUNT];
int remote_addr_lens[THREAD_COUNT] = {sizeof(struct sockaddr_in)};
pthread_mutex_t started_locks[THREAD_COUNT];
pthread_mutex_t finished_locks[THREAD_COUNT];

void terminate() {
    printf("\nTerminating\n");
    int true = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int));
    close(sock);
    exit(0);
}

void handle_interrupt(int signal) {
    terminate();
}

double calc_pi(uint64_t num_terms) {
    double pi = 0.0;
    int64_t denominator = 1;
    int64_t sign = 1;
    for(uint64_t i = 0; i < num_terms; i++) {
        pi += sign * (double) 4 / denominator;
        denominator += 2;
        sign *= -1;
    }
    return pi;
}

void * handler_thread(void * thread_num) {
    int id = *((int *)thread_num);
    int * buffer = buffers[id];
    struct sockaddr_in * remote_addr = &(remote_addrs[id]);
    int size;
    while(1) {
        pthread_mutex_lock(&(started_locks[id]));
//        size = recvfrom(sock, buffer, BUF_LEN*sizeof(int), 0, (struct sockaddr *) &remote_addr, &remote_addr_len);
        printf("%d got %d from %d\n", id, buffer[0], ntohs(remote_addr->sin_port));
        uint64_t num_terms = buffer[0];
        double pi = calc_pi(num_terms);
        ((double *) buffer)[0] = pi;
        size = sendto(sock, buffer, BUF_LEN*sizeof(int), 0, (struct sockaddr *) remote_addr, sizeof(*remote_addr));
        pthread_mutex_unlock(&(finished_locks[id]));
    }
    return NULL;
}

void empty() {}

int main(int argc, char * argv[]) {

    signal(SIGINT, handle_interrupt);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in self_addr;
    self_addr.sin_family = AF_INET;
    self_addr.sin_addr.s_addr = INADDR_ANY;

    for(int port = 30000; port < 31000; port++) {
        self_addr.sin_port = htons(port);
        int err = bind(sock, (struct sockaddr *) &self_addr, sizeof(self_addr));
        if(err) continue;
        printf("Setting up a server at port %d\n", port);
        break;
    }

    uint64_t num_terms = 0;
    if(argc > 1) num_terms = (uint64_t) strtoull(argv[1], NULL, 0);
    double pi = calc_pi(num_terms);
    printf("Pi at %ld terms - %.20lf\n", num_terms, pi);

    int indices[THREAD_COUNT];
    for(int i = 0; i < THREAD_COUNT; i++) {
        indices[i] = i;
        pthread_mutex_lock(&(started_locks[i]));
        pthread_mutex_unlock(&(finished_locks[i]));
        pthread_create(&(threads[i]), NULL, handler_thread, (void *) &(indices[i]));
    }

    int tid = 0;
    while(1) {
        int * buffer = buffers[tid];
        pthread_mutex_lock(&(finished_locks[tid]));
        remote_addr_lens[tid] = sizeof(struct sockaddr);
        int size = recvfrom(sock, buffer, BUF_LEN*sizeof(int), 0, (struct sockaddr *) &(remote_addrs[tid]), &(remote_addr_lens[tid]));
        pthread_mutex_unlock(&(started_locks[tid]));
        tid = (tid + 1) % THREAD_COUNT;
    }

    terminate();
    return 0;
}
