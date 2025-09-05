#include <arpa/inet.h>
#include <cassert>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <unistd.h>

#include <string>

#include "connector.hpp"
#include "epoll.hpp"
#include "listener.hpp"
#include "table.hpp"
#include "tools.hpp"

#define MAX_EVENTS 32
#define MAX_MSGS 32
#define BUFFER_SIZE 65536

int main(int argc, char **argv) {
    int return_code = EXIT_SUCCESS;

    struct mmsghdr msgs[MAX_MSGS];
    ::memset(&msgs, 0, sizeof(msgs));

    struct mmsghdr msgs_no_addr[MAX_MSGS];
    ::memset(&msgs_no_addr, 0, sizeof(msgs));

    struct mmsghdr msgs_common_addr[MAX_MSGS];
    ::memset(&msgs_common_addr, 0, sizeof(msgs));

    struct iovec iovecs_const[MAX_MSGS];
    ::memset(&iovecs_const, 0, sizeof(iovecs_const));

    struct iovec iovecs_mut[MAX_MSGS];
    ::memset(&iovecs_mut, 0, sizeof(iovecs_mut));

    struct sockaddr_in6 req_addrs[MAX_MSGS];
    ::memset(&req_addrs, 0, sizeof(req_addrs));

    struct sockaddr_in6 resp_common_addr;
    ::memset(&resp_common_addr, 0, sizeof(resp_common_addr));

    static unsigned char buffers[BUFFER_SIZE][MAX_MSGS];

    for (int i = 0; i < MAX_MSGS; i++) {
        iovecs_const[i].iov_base = buffers[i];
        iovecs_const[i].iov_len = BUFFER_SIZE;

        iovecs_mut[i].iov_base = buffers[i];
        iovecs_mut[i].iov_len = 0;

        msgs[i].msg_hdr.msg_name = &req_addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(req_addrs[i]);
        msgs[i].msg_hdr.msg_iov = &iovecs_const[i];
        msgs[i].msg_hdr.msg_iovlen = 1;

        msgs_no_addr[i].msg_hdr.msg_iov = &iovecs_const[i];
        msgs_no_addr[i].msg_hdr.msg_iovlen = 1;

        msgs_common_addr[i].msg_hdr.msg_name = &resp_common_addr;
        msgs_common_addr[i].msg_hdr.msg_namelen = sizeof(resp_common_addr);
        msgs_common_addr[i].msg_hdr.msg_iov = &iovecs_mut[i];
        msgs_common_addr[i].msg_hdr.msg_iovlen = 1;
    }

    if (argc != 7) {
        Tools::printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *source_port = argv[1];
    const char *endpoint_name = argv[2];
    const char *endpoint_port = argv[3];

    const time_t connection_timeout = std::stoull(argv[4]);
    const uint64_t in_mask = std::stoull(argv[5]);
    const uint64_t out_mask = std::stoull(argv[6]);
    const uint64_t mask = Tools::u64ToBe(in_mask ^ out_mask);

    Connector cnctr(endpoint_name, endpoint_port);

    struct sockaddr_storage bind_addr;
    int listen_fd = Listener::create(source_port, &bind_addr);
    int timer_fd = Tools::createTimer(connection_timeout);

    AddrTable table;

    Epoll epoll;
    epoll.add(listen_fd);
    epoll.add(timer_fd);

    struct epoll_event events[MAX_EVENTS];

    static sig_atomic_t exitFlag = 0;
    for (int signum : {SIGINT, SIGTERM, SIGHUP}) {
        if (::signal(signum, [](int) { exitFlag = 1; }) == SIG_ERR) {
            throw std::system_error(errno, std::generic_category(), "signal");
        }
    }

    std::cout << Tools::showSockaddr(reinterpret_cast<struct sockaddr *>(&bind_addr)) << " -> "
              << Tools::showSockaddr(cnctr.getAddr()) << std::endl;

    while (!exitFlag) {
        int num_events = epoll.wait(events, MAX_EVENTS, -1);
        if (num_events == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            return_code = EXIT_FAILURE;
            break;
        }

        for (int i = 0; i < num_events; i++) {
            const int sock = events[i].data.fd;
            if (sock == listen_fd) {
                const int msg_count = ::recvmmsg(listen_fd, msgs, MAX_MSGS, MSG_DONTWAIT, NULL);
                if (msg_count < 0) {
                    perror("recvmmsg");
                    continue;
                }

                for (int i = 0; i < msg_count; i += 1) {
                    uint8_t *const buf = (uint8_t *)msgs[i].msg_hdr.msg_iov->iov_base;
                    const size_t recv_len = msgs[i].msg_len;

                    if (mask) {
                        Tools::xor_block(
                            reinterpret_cast<uint64_t *>(buf),
                            (recv_len + sizeof(mask) - 1) /
                                sizeof(mask), // division with rounding up
                            mask
                        );
                    }

                    struct sockaddr_in6 &client_addr =
                        *reinterpret_cast<struct sockaddr_in6 *>(msgs[i].msg_hdr.msg_name);

                    auto sock = table.find(client_addr);
                    if (sock != nullptr) {
                        if (::send(*sock, buf, recv_len, 0) < 0) {
                            ::perror("send");
                            continue;
                        }
                    } else {
                        auto sock = cnctr.newConnection();
                        if (::send(sock, buf, recv_len, 0) < 0) {
                            ::perror("send");
                            ::close(sock);
                            continue;
                        }
                        table.add(sock, client_addr);
                        epoll.add(sock);
                    }
                }
            } else if (sock == timer_fd) {
                uint64_t expirations;
                if (::read(timer_fd, &expirations, sizeof(expirations)) != sizeof(expirations)) {
                    perror("read timer fd");
                    continue;
                }

                table.cleanup([&](int sock, const struct sockaddr_in6 &) {
                    epoll.del(sock);
                    close(sock);
                });
            } else {
                int msg_cnt = ::recvmmsg(sock, msgs_no_addr, MAX_MSGS, MSG_DONTWAIT, NULL);
                if (msg_cnt < 0) {
                    ::perror("recvmmsg");
                    epoll.del(sock);
                    table.erase(sock);
                    ::close(sock);
                    continue;
                }

                resp_common_addr = *table.find(sock);

                for (int i = 0; i < msg_cnt; i += 1) {
                    msgs_common_addr[i].msg_hdr.msg_iov->iov_len = msgs_no_addr[i].msg_len;
                    if (mask) {
                        Tools::xor_block(
                            reinterpret_cast<uint64_t *>(iovecs_mut[i].iov_base),
                            (iovecs_mut[i].iov_len + sizeof(mask) - 1) /
                                sizeof(mask), // division with rounding up
                            mask
                        );
                    }
                }

                if (::sendmmsg(listen_fd, msgs_common_addr, msg_cnt, 0) < 0) {
                    ::perror("sendmmsg");
                    continue;
                }
            }
        }
    }

    for (const auto &[sock, _] : table.s2a) {
        if (::close(sock) < 0) {
            ::perror("close");
            return_code = EXIT_FAILURE;
        }
    }

    if (::close(timer_fd) < 0) {
        ::perror("close");
        return_code = EXIT_FAILURE;
    }

    if (::close(listen_fd) < 0) {
        ::perror("close");
        return_code = EXIT_FAILURE;
    }

    std::cout << "Exit" << std::endl;

    return return_code;
}
