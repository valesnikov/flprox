#pragma once

#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

#include "tools.hpp"

class Connector {
  public:
    Connector(const char *hostname, const char *port) {
        struct addrinfo hints, *res, *p;
        ::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        auto status = ::getaddrinfo(hostname, port, &hints, &res);
        if (status != 0) {
            if (status != EAI_SYSTEM) {
                throw std::system_error(status, Tools::gai_category(), "getaddrinfo");
            } else {
                throw std::system_error(errno, std::generic_category(), "getaddrinfo");
            }
        }

        int sockfd = -1;

        for (p = res; p != nullptr; p = p->ai_next) {
            sockfd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sockfd == -1) {
                throw std::system_error(errno, std::generic_category(), "socket");
            }

            int err = ::connect(sockfd, p->ai_addr, p->ai_addrlen);
            ::close(sockfd);
            if (err < 0) {
                continue;
            }

            this->family = p->ai_family;
            this->socktype = p->ai_socktype;
            this->protocol = p->ai_protocol;
            this->addrlen = p->ai_addrlen;
            ::memcpy(&this->addr, p->ai_addr, p->ai_addrlen);
            ::freeaddrinfo(res);
            return;
        }

        ::freeaddrinfo(res);
        throw std::system_error(errno, std::generic_category(), "connect");
    }

    const struct sockaddr *getAddr() {
        return reinterpret_cast<struct sockaddr *>(&addr);
    }

    int newConnection() {
        auto sock = ::socket(family, socktype, protocol);
        if (sock < 0) {
            throw std::system_error(errno, std::generic_category(), "socket");
        }
        if (::connect(sock, (const struct sockaddr *)&addr, addrlen)) {
            ::close(sock);
            throw std::system_error(errno, std::generic_category(), "connect");
        }
        return sock;
    }

  private:
    int family;
    int socktype;
    int protocol;
    struct sockaddr_storage addr;
    socklen_t addrlen;
};