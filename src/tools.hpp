#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/timerfd.h>
#include <unistd.h>

struct Tools {
    static void printUsage(const char *prog_name) {
        std::cerr
            << "Usage: " << prog_name
            << " <source_port> <dest_hostname> <dest_port> <conn_timeout> <in_mask> <out_mask>"
            << std::endl
            << "mask is uint64 number, set to 0 if not required" << std::endl;
    }

    static uint64_t u64ToBe(uint64_t val) {
        uint8_t dst[sizeof(val)];
        for (int i = sizeof(val) - 1; i >= 0; i -= 1) {
            dst[i] = val & 0xff;
            val >>= 8;
        }
        ::memcpy(&val, dst, sizeof(val));
        return val;
    }

    static int createTimer(time_t interval) {
        int timer_fd = ::timerfd_create(CLOCK_MONOTONIC, 0);
        if (timer_fd == -1) {
            throw std::system_error(errno, std::generic_category(), "timerfd_create");
        }

        struct itimerspec timer_spec = {{interval, 0}, {interval, 0}};
        if (::timerfd_settime(timer_fd, 0, &timer_spec, NULL) == -1) {
            ::close(timer_fd);
            throw std::system_error(errno, std::generic_category(), "timerfd_settime");
        }
        return timer_fd;
    }

    static void xor_block(uint64_t *data, size_t len, uint64_t mask) {
        for (size_t i = 0; i < len; i++) {
            data[i] ^= mask;
        }
    }

    static std::string showSockaddr(const struct sockaddr *sa) {
        if (sa == nullptr) {
            return "null";
        }

        char str[INET6_ADDRSTRLEN] = {0};

        switch (sa->sa_family) {
        case AF_INET: {
            const auto *sin = reinterpret_cast<const sockaddr_in *>(sa);
            if (::inet_ntop(AF_INET, &(sin->sin_addr), str, INET_ADDRSTRLEN) == nullptr) {
                throw std::system_error(errno, std::generic_category(), "inet_ntop");
            }
            return std::string(str) + ":" + std::to_string(::ntohs(sin->sin_port));
        }
        case AF_INET6: {
            const auto *sin6 = reinterpret_cast<const sockaddr_in6 *>(sa);
            if (::inet_ntop(AF_INET6, &(sin6->sin6_addr), str, INET6_ADDRSTRLEN) == nullptr) {
                throw std::system_error(errno, std::generic_category(), "inet_ntop");
            }
            return "[" + std::string(str) + "]:" + std::to_string(::ntohs(sin6->sin6_port));
        }
        default:
            return "unknown address family";
        }
    }

    class gai_category : public std::error_category {
      public:
        const char *name() const noexcept override {
            return "getaddrinfo";
        }

        std::string message(int ev) const override {
            return gai_strerror(ev);
        }
    };
};
