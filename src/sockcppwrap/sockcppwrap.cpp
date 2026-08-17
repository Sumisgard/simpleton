#include "sockcppwrap.h"
#include <algorithm>
#include <array>

namespace Simple
{

void SockCppWrap::check_error(int result, const std::string& operation) const
{
    if (result == -1) {
        throw NetworkException(operation, errno);
    }
}

void SockCppWrap::validate_socket_state() const
{
    if (m_socket_fd == -1) {
        throw NetworkException("Invalid socket descriptor");
    }
}

SockCppWrap::SockCppWrap(AddressFamily family, SocketType type, int protocol)
    : m_socket_fd(-1)
    , m_is_connected(false)
    , m_is_listening(false)
    , m_family(family)
    , m_type(type)
{
    m_socket_fd = socket(static_cast<int>(family), static_cast<int>(type), protocol);
    if (m_socket_fd == -1) {
        throw NetworkException("socket() failed", errno);
    }
}

SockCppWrap::~SockCppWrap()
{
    close();
}

SockCppWrap::SockCppWrap(SockCppWrap&& other) noexcept
    : m_socket_fd(other.m_socket_fd)
    , m_is_connected(other.m_is_connected)
    , m_is_listening(other.m_is_listening)
    , m_family(other.m_family)
    , m_type(other.m_type)
{
    other.m_socket_fd = -1;
    other.m_is_connected = false;
    other.m_is_listening = false;
}

SockCppWrap& SockCppWrap::operator=(SockCppWrap&& other) noexcept
{
    if (this != &other) {
        close();
        m_socket_fd = other.m_socket_fd;
        m_is_connected = other.m_is_connected;
        m_is_listening = other.m_is_listening;
        m_family = other.m_family;
        m_type = other.m_type;
        
        other.m_socket_fd = -1;
        other.m_is_connected = false;
        other.m_is_listening = false;
    }
    return *this;
}

void SockCppWrap::close() 
{
    if (m_socket_fd != -1) {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        m_is_connected = false;
        m_is_listening = false;
    }
}

void SockCppWrap::bind(const std::string& address, int port)
{
    validate_socket_state();
    
    if (m_is_connected || m_is_listening) {
        throw NetworkException("Cannot bind: socket already in use");
    }

    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } addr;
    
    std::memset(&addr, 0, sizeof(addr));
    
    if (is_valid_ipv4(address)) {
        addr.addr4.sin_family = AF_INET;
        addr.addr4.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &addr.addr4.sin_addr) <= 0) {
            throw NetworkException("Invalid IPv4 address: " + address);
        }
        check_error(::bind(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr.addr4), sizeof(addr.addr4)), 
                    "bind() failed");
    } else if (is_valid_ipv6(address)) {
        addr.addr6.sin6_family = AF_INET6;
        addr.addr6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, address.c_str(), &addr.addr6.sin6_addr) <= 0) {
            throw NetworkException("Invalid IPv6 address: " + address);
        }
        check_error(::bind(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr.addr6), sizeof(addr.addr6)), 
                    "bind() failed");
    } else {
        throw NetworkException("Invalid address format: " + address);
    }
}

void SockCppWrap::listen(int backlog) 
{
    validate_socket_state();
    
    if (static_cast<int>(m_type) != SOCK_STREAM) {
        throw NetworkException("listen() can only be called on stream sockets");
    }
    
    check_error(::listen(m_socket_fd, backlog), "listen() failed");
    m_is_listening = true;
}

SockCppWrap SockCppWrap::accept()
{
    validate_socket_state();
    
    if (!m_is_listening) {
        throw NetworkException("Socket must be listening before accept()");
    }

    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } client_addr;
    
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(m_socket_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd == -1) {
        throw NetworkException("accept() failed", errno);
    }

    SockCppWrap client(m_family, m_type);
    client.m_socket_fd = client_fd;
    client.m_is_connected = true;
    return client;
}

void SockCppWrap::connect(const std::string& address, int port)
{
    validate_socket_state();
    
    if (m_is_connected) {
        throw NetworkException("Socket already connected");
    }

    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } addr;
    
    std::memset(&addr, 0, sizeof(addr));

    if (is_valid_ipv4(address)) {
        addr.addr4.sin_family = AF_INET;
        addr.addr4.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &addr.addr4.sin_addr) <= 0) {
            throw NetworkException("Invalid IPv4 address: " + address);
        }
        check_error(::connect(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr.addr4), sizeof(addr.addr4)), 
                    "connect() failed");
    } else if (is_valid_ipv6(address)) {
        addr.addr6.sin6_family = AF_INET6;
        addr.addr6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, address.c_str(), &addr.addr6.sin6_addr) <= 0) {
            throw NetworkException("Invalid IPv6 address: " + address);
        }
        check_error(::connect(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr.addr6), sizeof(addr.addr6)), 
                    "connect() failed");
    } else {
        throw NetworkException("Invalid address format: " + address);
    }
    
    m_is_connected = true;
}

size_t SockCppWrap::send(const std::string& data, int flags)
{
    return send(data.c_str(), data.length(), flags);
}

size_t SockCppWrap::send(const void* data, size_t length, int flags)
{
    validate_socket_state();
    
    if (!m_is_connected) {
        throw NetworkException("Socket not connected");
    }
    
    ssize_t sent = ::send(m_socket_fd, data, length, flags);
    if (sent == -1) {
        throw NetworkException("send() failed", errno);
    }
    return static_cast<size_t>(sent);
}

std::string SockCppWrap::recv(size_t buffer_size, int flags)
{
    std::string buffer(buffer_size, '\0');
    size_t received = recv(&buffer[0], buffer_size, flags);
    buffer.resize(received);
    return buffer;
}

size_t SockCppWrap::recv(void* buffer, size_t buffer_size, int flags)
{
    validate_socket_state();
    
    if (!m_is_connected) {
        throw NetworkException("Socket not connected");
    }
    
    ssize_t received = ::recv(m_socket_fd, buffer, buffer_size, flags);
    if (received == -1) {
        throw NetworkException("recv() failed", errno);
    }
    if (received == 0) {
        m_is_connected = false;
    }
    return static_cast<size_t>(received);
}

size_t SockCppWrap::send_to(const void* data, size_t length, const std::string& address, int port, int flags)
{
    validate_socket_state();
    
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } dest_addr;
    
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    
    if (is_valid_ipv4(address)) {
        dest_addr.addr4.sin_family = AF_INET;
        dest_addr.addr4.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &dest_addr.addr4.sin_addr) <= 0) {
            throw NetworkException("Invalid IPv4 address: " + address);
        }
        ssize_t sent = ::sendto(m_socket_fd, data, length, flags, 
                               reinterpret_cast<struct sockaddr*>(&dest_addr.addr4), sizeof(dest_addr.addr4));
        if (sent == -1) {
            throw NetworkException("sendto() failed", errno);
        }
        return static_cast<size_t>(sent);
    } else if (is_valid_ipv6(address)) {
        dest_addr.addr6.sin6_family = AF_INET6;
        dest_addr.addr6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, address.c_str(), &dest_addr.addr6.sin6_addr) <= 0) {
            throw NetworkException("Invalid IPv6 address: " + address);
        }
        ssize_t sent = ::sendto(m_socket_fd, data, length, flags, 
                               reinterpret_cast<struct sockaddr*>(&dest_addr.addr6), sizeof(dest_addr.addr6));
        if (sent == -1) {
            throw NetworkException("sendto() failed", errno);
        }
        return static_cast<size_t>(sent);
    } else {
        throw NetworkException("Invalid address format: " + address);
    }
}

size_t SockCppWrap::recv_from(void* buffer, size_t buffer_size, std::string& sender_address, int& sender_port, int flags)
{
    validate_socket_state();
    
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } src_addr;
    
    socklen_t addr_len = sizeof(src_addr);
    
    ssize_t received = ::recvfrom(m_socket_fd, buffer, buffer_size, flags,
                                 reinterpret_cast<struct sockaddr*>(&src_addr), &addr_len);
    if (received == -1) {
        throw NetworkException("recvfrom() failed", errno);
    }
    
    char ip_str[INET6_ADDRSTRLEN];
    if (src_addr.addr4.sin_family == AF_INET) {
        inet_ntop(AF_INET, &src_addr.addr4.sin_addr, ip_str, sizeof(ip_str));
        sender_address = std::string(ip_str);
        sender_port = ntohs(src_addr.addr4.sin_port);
    } else if (src_addr.addr6.sin6_family == AF_INET6) {
        inet_ntop(AF_INET6, &src_addr.addr6.sin6_addr, ip_str, sizeof(ip_str));
        sender_address = std::string(ip_str);
        sender_port = ntohs(src_addr.addr6.sin6_port);
    }
    
    return static_cast<size_t>(received);
}

void SockCppWrap::set_nonblocking(bool non_blocking)
{
    validate_socket_state();
    
    int flags = fcntl(m_socket_fd, F_GETFL, 0);
    if (flags == -1) {
        throw NetworkException("fcntl(F_GETFL) failed", errno);
    }
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    check_error(fcntl(m_socket_fd, F_SETFL, flags), "fcntl(F_SETFL) failed");
}

void SockCppWrap::set_reuse_address(bool reuse)
{
    validate_socket_state();
    
    int opt = reuse ? 1 : 0;
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, 
                          &opt, sizeof(opt)), "setsockopt(SO_REUSEADDR) failed");
}

void SockCppWrap::set_reuse_port(bool reuse)
{
    validate_socket_state();
    
#ifdef SO_REUSEPORT
    int opt = reuse ? 1 : 0;
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEPORT, 
                          &opt, sizeof(opt)), "setsockopt(SO_REUSEPORT) failed");
#else
    throw NetworkException("SO_REUSEPORT not supported on this platform");
#endif
}

void SockCppWrap::set_send_timeout(std::chrono::milliseconds timeout)
{
    validate_socket_state();
    
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_SNDTIMEO, 
                          &tv, sizeof(tv)), "setsockopt(SO_SNDTIMEO) failed");
}

void SockCppWrap::set_receive_timeout(std::chrono::milliseconds timeout)
{
    validate_socket_state();
    
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVTIMEO, 
                          &tv, sizeof(tv)), "setsockopt(SO_RCVTIMEO) failed");
}

void SockCppWrap::set_nodelay(bool nodelay)
{
    validate_socket_state();
    
    int opt = nodelay ? 1 : 0;
    check_error(setsockopt(m_socket_fd, IPPROTO_TCP, TCP_NODELAY, 
                          &opt, sizeof(opt)), "setsockopt(TCP_NODELAY) failed");
}

void SockCppWrap::set_keepalive(bool keepalive)
{
    validate_socket_state();
    
    int opt = keepalive ? 1 : 0;
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_KEEPALIVE, 
                          &opt, sizeof(opt)), "setsockopt(SO_KEEPALIVE) failed");
}

void SockCppWrap::set_send_buffer_size(size_t size)
{
    validate_socket_state();
    
    int opt = static_cast<int>(size);
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_SNDBUF, 
                          &opt, sizeof(opt)), "setsockopt(SO_SNDBUF) failed");
}

void SockCppWrap::set_receive_buffer_size(size_t size)
{
    validate_socket_state();
    
    int opt = static_cast<int>(size);
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVBUF, 
                          &opt, sizeof(opt)), "setsockopt(SO_RCVBUF) failed");
}

void SockCppWrap::set_linger(bool enable, int seconds)
{
    validate_socket_state();
    
    struct linger ling;
    ling.l_onoff = enable ? 1 : 0;
    ling.l_linger = seconds;
    check_error(setsockopt(m_socket_fd, SOL_SOCKET, SO_LINGER, 
                          &ling, sizeof(ling)), "setsockopt(SO_LINGER) failed");
}

void SockCppWrap::shutdown(ShutdownHow how) {
    if (m_socket_fd != -1) {
        ::shutdown(m_socket_fd, static_cast<int>(how));
    }
}

std::pair<std::string, int> SockCppWrap::get_local_address() const
{
    validate_socket_state();
    
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } addr;
    
    socklen_t addr_len = sizeof(addr);
    if (getsockname(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) == -1) {
        throw NetworkException("getsockname() failed", errno);
    }
    
    char ip_str[INET6_ADDRSTRLEN];
    int port;
    
    if (addr.addr4.sin_family == AF_INET) {
        inet_ntop(AF_INET, &addr.addr4.sin_addr, ip_str, sizeof(ip_str));
        port = ntohs(addr.addr4.sin_port);
    } else {
        inet_ntop(AF_INET6, &addr.addr6.sin6_addr, ip_str, sizeof(ip_str));
        port = ntohs(addr.addr6.sin6_port);
    }
    
    return std::make_pair(std::string(ip_str), port);
}

std::pair<std::string, int> SockCppWrap::get_remote_address() const
{
    validate_socket_state();
    
    union {
        struct sockaddr_in addr4;
        struct sockaddr_in6 addr6;
    } addr;
    
    socklen_t addr_len = sizeof(addr);
    if (getpeername(m_socket_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) == -1) {
        throw NetworkException("getpeername() failed", errno);
    }
    
    char ip_str[INET6_ADDRSTRLEN];
    int port;
    
    if (addr.addr4.sin_family == AF_INET) {
        inet_ntop(AF_INET, &addr.addr4.sin_addr, ip_str, sizeof(ip_str));
        port = ntohs(addr.addr4.sin_port);
    } else {
        inet_ntop(AF_INET6, &addr.addr6.sin6_addr, ip_str, sizeof(ip_str));
        port = ntohs(addr.addr6.sin6_port);
    }
    
    return std::make_pair(std::string(ip_str), port);
}

bool SockCppWrap::is_valid_ipv4(const std::string& address)
{
    struct in_addr addr4;
    return inet_pton(AF_INET, address.c_str(), &addr4) == 1;
}

bool SockCppWrap::is_valid_ipv6(const std::string& address)
{
    struct in6_addr addr6;
    return inet_pton(AF_INET6, address.c_str(), &addr6) == 1;
}

} // namespace Simple