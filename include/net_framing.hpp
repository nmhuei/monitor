#pragma once
/*
 * TCP message framing: [4-byte big-endian length][payload bytes]
 */
#include <string>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>
#include <vector>

namespace monitor::net {

// Fully send all bytes, return false on error
inline bool sendAll(int fd, const void* buf, size_t len) {
    const char* p = reinterpret_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p   += n;
        len -= n;
    }
    return true;
}

// Fully recv all bytes, return false on EOF/error
inline bool recvAll(int fd, void* buf, size_t len) {
    char* p = reinterpret_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = ::recv(fd, p, len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p   += n;
        len -= n;
    }
    return true;
}

// Send a framed message; checks for server-side close before writing
inline bool sendMsg(int fd, const std::string& payload) {
    // Peek 1 byte — if recv returns 0 the peer closed the connection,
    // if it returns -1 with EAGAIN the socket is still alive (no data ready).
    char peek = 0;
    ssize_t peeked = ::recv(fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
    if (peeked == 0) return false;           // FIN received — server closed
    if (peeked < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return false;                        // real socket error

    uint32_t netLen = htonl(static_cast<uint32_t>(payload.size()));
    if (!sendAll(fd, &netLen, 4)) return false;
    return sendAll(fd, payload.data(), payload.size());
}

// Receive a framed message (blocks); returns empty string on error
inline std::string recvMsg(int fd) {
    uint32_t netLen = 0;
    if (!recvAll(fd, &netLen, 4)) return {};
    uint32_t len = ntohl(netLen);
    if (len == 0 || len > 4 * 1024 * 1024) return {}; // sanity
    std::string buf(len, '\0');
    if (!recvAll(fd, buf.data(), len)) return {};
    return buf;
}

} // namespace monitor::net
