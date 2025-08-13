#pragma once

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

#include "tools.hpp"

struct Listener {
    static int create(const char *port, struct sockaddr_storage *addr) {
        struct addrinfo hints;
        ::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        
        struct addrinfo *res;
        auto status = ::getaddrinfo(NULL, port, &hints, &res);
        if (status != 0) {
            if (status != EAI_SYSTEM) {
                throw std::system_error(status, Tools::gai_category(), "getaddrinfo");
            } else {
                throw std::system_error(errno, std::generic_category(), "getaddrinfo");
            }
        }

        struct addrinfo *p;

        int sockfd = -1;
        for (p = res; p != NULL; p = p->ai_next) {
            sockfd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sockfd == -1) {
                throw std::system_error(errno, std::generic_category(), "socket");
            }

            const int yes = 1;
            if (::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
                ::close(sockfd);
                throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");
            }

            const int no = 0;
            if (::setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) < 0) {
                ::close(sockfd);
                throw std::system_error(errno, std::generic_category(), "setsockopt IPV6_V6ONLY");
            }

            if (::bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                ::close(sockfd);
                continue;
            }

            break;
        }
        ::memcpy(addr, p->ai_addr, p->ai_addrlen);
        ::freeaddrinfo(res);

        if (p == NULL) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }

        return sockfd;
    }
};
