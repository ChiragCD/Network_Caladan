/*
 * netperf.c - a client similar to netperf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>

/* #include <base/stddef.h> */
#include <base/log.h>
#include <base/time.h>
#include <net/ip.h>
#include <runtime/runtime.h>
#include <runtime/sync.h>
#include <runtime/udp.h>

#include <pthread.h>

#define NETPERF_PORT	8000

const long NS_PER_S = 1000000000;

/* experiment parameters */
static struct netaddr raddr;
static int nworkers;
static int seconds;
static uint64_t stop_us;
static size_t payload_len;
static int depth;
static struct client_rr_args *arg_tbl;

pthread_mutex_t server_lock;

#define BUF_SIZE	32768

struct client_rr_args {
    int id;
    waitgroup_t *wg;
    uint64_t * starts;
    uint64_t ** ends;
    uint64_t reqs;
    udpconn_t * c;
    // packet rate in packets per second
    uint64_t packet_rate;
    struct netaddr laddr;
};

void terminate() {
    printf("\nTerminating\n");
    FILE * file = fopen("out", "w");
    for(int i = 1; i < nworkers; i++) {
        for(int j = 0; j < arg_tbl[i].reqs; j++) {
            uint64_t start = arg_tbl[i].starts[j];
            uint64_t end = arg_tbl[0].ends[i][j];
            uint64_t latency = 0;
            if(end) latency = end - start;
            fprintf(file, "%d %d %ld %ld %ld\n", i, j, start, end, latency);
        }
    }
    fclose(file);
    exit(0);
}

void handle_interrupt(int signal) {
    terminate();
}

static void client_receiver(void * arg) {
	unsigned char buf[BUF_SIZE];
	struct client_rr_args *args = (struct client_rr_args *)arg;
    args->ends = (uint64_t **) malloc(nworkers * sizeof(uint64_t *));
    for(int i = 0; i < nworkers; i++) args->ends[i] = (uint64_t *) malloc(100000 * sizeof(uint64_t));
    for(int i = 0; i < nworkers; i++) memset(args->ends[i], 0, 100000 * sizeof(uint64_t));
    struct timespec t1, t2;
    ssize_t ret;

	while (microtime() < stop_us + 10000) {
		ret = udp_read(args->c, buf, payload_len);
		if (ret != -11 && (ret <= 0 || ret % payload_len != 0)) {
			printf("udp_read() failed, ret = %ld\n", ret);
			break;
		}
        if(ret != 11) {
            uint64_t request_number = ((uint64_t *)buf)[1];
            uint64_t id = ((uint64_t *)buf)[2];
            args->ends[id][request_number] = microtime();
        }
	}

	printf("close port %hu\n", udp_local_addr(args->c).port);
	waitgroup_done(args->wg);
}

static void client_worker(void *arg)
{
	unsigned char buf[BUF_SIZE];
	struct client_rr_args *args = (struct client_rr_args *)arg;
	args->starts = (uint64_t *) malloc(100000 * sizeof(uint64_t));
	memset(args->starts, 0, 100000 * sizeof(uint64_t));
	long ns_per_packet = NS_PER_S / args->packet_rate;
	struct timespec t1, t2;
	int ret;

	while (microtime() < stop_us) {
		((uint64_t *)buf)[0] = 400;
		((uint64_t *)buf)[1] = args->reqs;
		((uint64_t *)buf)[2] = args->id;
		((uint64_t *)buf)[3] = args->id;
		args->starts[args->reqs] = microtime();
		args->reqs += 1;
		ret = udp_send(buf, payload_len, args->laddr, raddr);
		// ret = udp_write(args->c, buf, payload_len);
		if (ret != -11 && (ret != payload_len)) {
			printf("udp_write() failed, ret = %d\n", ret);
			break;
		}

		t1.tv_sec = 0;
		t1.tv_nsec = ns_per_packet;
		while(nanosleep(&t1, &t2)) {
			t1.tv_nsec = t2.tv_nsec;
		}
	}

	printf("close port %hu\n", udp_local_addr(args->c).port);
	waitgroup_done(args->wg);
}

void par_client_receiver(struct udp_spawn_data * arg) {
    uint64_t request_number = ((uint64_t *)arg->buf)[1];
    uint64_t id = ((uint64_t *)arg->buf)[2];
    arg_tbl[0].ends[id][request_number] = microtime();
}

static void do_client(void *arg)
{
	waitgroup_t wg;
	int i, ret;
	uint64_t reqs = 0;

	printf("client-mode UDP: %d workers, %ld bytes, %d seconds %d depth\n",
		 nworkers, payload_len, seconds, depth);

	arg_tbl = calloc(nworkers, sizeof(*arg_tbl));
	BUG_ON(!arg_tbl);

	waitgroup_init(&wg);
	waitgroup_add(&wg, nworkers);
	stop_us = microtime() + seconds * ONE_SECOND;
    udpconn_t *c;
    struct netaddr laddr;
    laddr.ip = 0;
    laddr.port = NETPERF_PORT;
    // if (ret = udp_dial(laddr, raddr, &c)) {
    //     printf("udp_dial() failed, ret = %d\n", ret);
    //     return;
    // }
    // laddr.port = udp_local_addr(c).port;

	udpspawner_t * spawner;

    ret = udp_create_spawner(laddr, par_client_receiver, &spawner);
    BUG_ON(ret);

    arg_tbl[0].ends = (uint64_t **) malloc(nworkers * sizeof(uint64_t *));
    for(int i = 0; i < nworkers; i++) arg_tbl[0].ends[i] = (uint64_t *) malloc(100000 * sizeof(uint64_t));
    for(int i = 0; i < nworkers; i++) memset(arg_tbl[0].ends[i], 0, 100000 * sizeof(uint64_t));
    for (i = 0; i < nworkers; i++) {
	    arg_tbl[i].c = c;
	    arg_tbl[i].wg = &wg;
	    arg_tbl[i].reqs = 0;
	    arg_tbl[i].id = i;
	    arg_tbl[i].laddr = laddr;
	    arg_tbl[i].packet_rate = 50 * 1000 * 1000;
	    if(i) {
		    ret = thread_spawn(client_worker, &arg_tbl[i]);
	    }
	    else {
		    // ret = thread_spawn(client_receiver, &arg_tbl[i]);
	    }
	    BUG_ON(ret);
    }

    waitgroup_wait(&wg);
    udp_shutdown(arg_tbl[0].c);
    udp_close(arg_tbl[0].c);

    for (i = 0; i < nworkers; i++) {
	    printf("%d made %ld reqs\n", i, arg_tbl[i].reqs);
	    reqs += arg_tbl[i].reqs;
    }

    printf("measured %f reqs/s\n", (double)reqs / seconds);
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

static void server_worker(struct udp_spawn_data * arg)
{
	/* calculate pi and return it */
    uint64_t num_terms = *(uint64_t *)arg->buf;
    double pi = calc_pi(num_terms);
    ((uint64_t *)arg->buf)[0] = *(uint64_t *)(&pi);
    ((uint64_t *)arg->buf)[3] = 0;
    arg->raddr.port = NETPERF_PORT;
    ssize_t ret = udp_send(arg->buf, payload_len, arg->laddr, arg->raddr);
}

static void do_server(void *arg)
{
	struct netaddr laddr;
	int ret;

	laddr.ip = 0;
	laddr.port = NETPERF_PORT;

	udpspawner_t * spawner;

    ret = udp_create_spawner(laddr, server_worker, &spawner);
    BUG_ON(ret);

    // Block forever
    pthread_mutex_lock(&server_lock);
}

static int str_to_ip(const char *str, uint32_t *addr)
{
	uint8_t a, b, c, d;
	if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
		return -EINVAL;
	}

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}

static int str_to_long(const char *str, long *val)
{
	char *endptr;

	*val = strtol(str, &endptr, 10);
	if (endptr == str || (*endptr != '\0' && *endptr != '\n') ||
	    ((*val == LONG_MIN || *val == LONG_MAX) && errno == ERANGE))
		return -EINVAL;
	return 0;
}

int main(int argc, char *argv[])
{

    signal(SIGINT, handle_interrupt);
	int ret;
	long tmp;
	uint32_t addr;
	thread_fn_t fn;

    pthread_mutex_lock(&server_lock);

	if (argc < 8) {
		printf("%s: [config_file_path] [mode] [nworkers] [ip] [time] "
		       "[payload_len] [depth]\n", argv[0]);
		return -EINVAL;
	}

	if (!strcmp(argv[2], "CLIENT")) {
		fn = do_client;
	} else if (!strcmp(argv[2], "SERVER")) {
		fn = do_server;
	} else {
		printf("invalid mode '%s'\n", argv[2]);
		return -EINVAL;
	}

	ret = str_to_long(argv[3], &tmp);
	if (ret) {
		printf("couldn't parse [nworkers] '%s'\n", argv[3]);
		return -EINVAL;
	}
	nworkers = tmp;

	ret = str_to_ip(argv[4], &addr);
	if (ret) {
		printf("couldn't parse [ip] '%s'\n", argv[4]);
		return -EINVAL;
	}
	raddr.ip = addr;
	raddr.port = NETPERF_PORT;

	ret = str_to_long(argv[5], &tmp);
	if (ret) {
		printf("couldn't parse [time] '%s'\n", argv[5]);
		return -EINVAL;
	}
	seconds = tmp;

	ret = str_to_long(argv[6], &tmp);
	if (ret) {
		printf("couldn't parse [payload_len] '%s'\n", argv[6]);
		return -EINVAL;
	}
	payload_len = tmp;

	ret = str_to_long(argv[7], &tmp);
	if (ret) {
		printf("couldn't parse [depth] '%s'\n", argv[7]);
		return -EINVAL;
	}
	depth = tmp;

	ret = runtime_init(argv[1], fn, NULL);
	if (ret) {
		printf("failed to start runtime\n");
		return ret;
	}

	return 0;
}

