#pragma once

#include <sys/epoll.h>
#include <system_error>
#include <unistd.h>

class Epoll {
  public:
    Epoll()
        : epollFd(::epoll_create1(0)) {
        if (epollFd == -1) {
            throw std::system_error(errno, std::generic_category(), "epoll_create1(0)");
        }
    }

    Epoll &operator=(const Epoll &) = delete;
    Epoll &operator=(Epoll &&) = delete;
    Epoll(const Epoll &) = delete;
    Epoll(Epoll &&) = delete;

    void add(int fd) {
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = fd;
        if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) < 0) {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl EPOLL_CTL_ADD");
        }
    }

    void del(int fd) {
        if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL) < 0) {
            throw std::system_error(errno, std::generic_category(), "epoll_ctl EPOLL_CTL_DEL");
        }
    }

    ~Epoll() {
        ::close(epollFd);
    }

    int wait(struct epoll_event *events, int maxevents, int timeout) {
        return epoll_wait(epollFd, events, maxevents, timeout);
    }

  private:
    const int epollFd;
};