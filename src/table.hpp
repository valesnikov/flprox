#pragma once

#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <string_view>
#include <unordered_map>

[[maybe_unused]]
static bool operator==(const struct sockaddr_in6 &a, const struct sockaddr_in6 &b) {
    return ::memcmp(&a, &b, sizeof(struct sockaddr_in6)) == 0;
}

namespace std {
template <> struct hash<struct sockaddr_in6> {
    size_t operator()(const struct sockaddr_in6 &addr) const {
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char *>(&addr), sizeof(addr))
        );
    }
};
} // namespace std

using std::tuple;
using std::unordered_map;

class AddrTable {
  public:
    AddrTable() {
        s2a.max_load_factor(0.7);
        a2s.max_load_factor(0.7);
    }

    void add(int s, const struct sockaddr_in6 &a) {
        s2a[s] = {a, true};
        a2s[a] = {s, true};
    }

    struct sockaddr_in6 *find(int s) {
        auto it = s2a.find(s);
        if (it != s2a.end()) {
            auto &[addr, used] = it->second;
            used = true;
            return &addr;
        } else {
            return nullptr;
        }
    }

    int *find(const struct sockaddr_in6 &s) {
        auto it = a2s.find(s);
        if (it != a2s.end()) {
            auto &[addr, used] = it->second;
            used = true;
            return &addr;
        } else {
            return nullptr;
        }
    }

    const struct sockaddr_in6 &get(int s) {
        auto &[addr, used] = s2a[s];
        used = true;
        return addr;
    }

    int get(const struct sockaddr_in6 &a) {
        auto &[sock, used] = a2s[a];
        used = true;
        return sock;
    }

    void erase(int s) {
        auto &[addr, _] = s2a[s];
        a2s.erase(addr);
        s2a.erase(s);
    }

    void erase(const struct sockaddr_in6 &a) {
        auto &[sock, _] = a2s[a];
        s2a.erase(sock);
        a2s.erase(a);
    }

    void cleanup(std::function<void(int, const struct sockaddr_in6 &)> onDelete) {
        for (auto it = a2s.begin(); it != a2s.end();) {
            auto &[sock, used_a2s] = it->second;
            auto &[addr, used_s2a] = s2a[sock];
            if (used_a2s || used_s2a) { // if active
                used_a2s = false;
                used_s2a = false;
                it++;
            } else {
                onDelete(sock, addr);
                s2a.erase(sock);
                it = a2s.erase(it);
            }
        }
        if (a2s.load_factor() <= 0.1) {
            a2s.rehash(0);
        }
        if (s2a.load_factor() <= 0.1) {
            s2a.rehash(0);
        }
    }

    unordered_map<int, tuple<struct sockaddr_in6, bool>> s2a;
    unordered_map<struct sockaddr_in6, tuple<int, bool>> a2s;
};
