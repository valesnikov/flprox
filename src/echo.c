#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_MSGS 32
#define BUFFER_SIZE 65536

static volatile sig_atomic_t shutdown_flag = false;

static void signal_handler(int signal) {
    (void)signal;
    shutdown_flag = true;
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

int main(int argc, char **argv) {
    int sockfd;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    int rv;
    if ((rv = getaddrinfo(NULL, argv[1], &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(EXIT_FAILURE);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }

        int no = 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) == -1) {
            perror("setsockopt IPV6_V6ONLY");
            close(sockfd);
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to bind\n");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);
    printf("UDP echo server started on port %s\n", argv[1]);

    struct mmsghdr msgs[MAX_MSGS];
    memset(&msgs, 0, sizeof(msgs));

    struct iovec iovecs[MAX_MSGS];
    memset(&iovecs, 0, sizeof(iovecs));

    static unsigned char buffers[MAX_MSGS][BUFFER_SIZE];

    struct sockaddr_in6 addrs[MAX_MSGS];
    memset(&addrs, 0, sizeof(addrs));

    for (int i = 0; i < MAX_MSGS; i++) {
        iovecs[i].iov_base = buffers[i];
        iovecs[i].iov_len = BUFFER_SIZE;

        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in6);
    }

    setup_signal_handlers();

    while (1) {
        int num_received = recvmmsg(sockfd, msgs, MAX_MSGS, MSG_WAITFORONE, NULL);
        if (num_received < 0) {
            if (errno == EINTR && shutdown_flag == true) {
                goto exit;
            }
            perror("recvmmsg error");
            continue;
        }

        for (int i = 0; i < num_received; i++) {
            iovecs[i].iov_len = msgs[i].msg_len;
        }

        int num_sent = sendmmsg(sockfd, msgs, num_received, 0);
        if (num_sent != num_received) {
            if (errno == EINTR && shutdown_flag == true) {
                goto exit;
            }
            printf("Sent %d/%d messages\n", num_sent, num_received);
        }

        for (int i = 0; i < num_received; i++) {
            iovecs[i].iov_len = BUFFER_SIZE;
        }
    }
exit:
    puts("exit");
    close(sockfd);
    return 0;
}
