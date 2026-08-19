#include "sockcppwrap.h"

#include <limits>
#include <cstdint>
#include <sys/time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace Simple
{

void SockCppWrap::check_error(int result, const std::string& operation) const
{
    if (result == -1)
    {
        throw NetworkException(operation, errno);
    }
}

void SockCppWrap::validate_socket_state() const
{
    if (m_socket_fd == -1)
    {
        throw NetworkException("Invalid socket descriptor", EBADF);
    }
}

bool SockCppWrap::make_address(const std::string& address,
                               int port,
                               struct sockaddr_storage& storage,
                               socklen_t& length)
{
    if (port < 0 || port > 65535)
    {
        return false;
    }

    std::memset(&storage, 0, sizeof(storage));

    const std::uint16_t net_port = htons(static_cast<std::uint16_t>(port));

    if (is_valid_ipv4(address))
    {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&storage);
        addr4->sin_family = AF_INET;
        addr4->sin_port = net_port;

        if (::inet_pton(AF_INET, address.c_str(), &addr4->sin_addr) != 1)
        {
            return false;
        }

        length = sizeof(*addr4);
        return true;
    }

    if (is_valid_ipv6(address))
    {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&storage);
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = net_port;

        if (::inet_pton(AF_INET6, address.c_str(), &addr6->sin6_addr) != 1)
        {
            return false;
        }

        length = sizeof(*addr6);
        return true;
    }

    return false;
}

std::pair<std::string, int> SockCppWrap::format_address(
    const struct sockaddr_storage& storage)
{
    char host[INET6_ADDRSTRLEN] = {};

    if (storage.ss_family == AF_INET)
    {
        const auto* addr4 = reinterpret_cast<const struct sockaddr_in*>(&storage);

        if (::inet_ntop(AF_INET, &addr4->sin_addr, host, sizeof(host)) == nullptr)
        {
            throw NetworkException("inet_ntop() failed", errno);
        }

        return {std::string(host), static_cast<int>(ntohs(addr4->sin_port))};
    }

    if (storage.ss_family == AF_INET6)
    {
        const auto* addr6 = reinterpret_cast<const struct sockaddr_in6*>(&storage);

        if (::inet_ntop(AF_INET6, &addr6->sin6_addr, host, sizeof(host)) == nullptr)
        {
            throw NetworkException("inet_ntop() failed", errno);
        }

        return {std::string(host), static_cast<int>(ntohs(addr6->sin6_port))};
    }

    throw NetworkException("Unsupported address family", EAFNOSUPPORT);
}

SockCppWrap::SockCppWrap(AddressFamily family, SocketType type, int protocol)
    : m_socket_fd(-1)
    , m_is_connected(false)
    , m_is_listening(false)
    , m_family(family)
    , m_type(type)
{
    m_socket_fd = ::socket(static_cast<int>(family), static_cast<int>(type), protocol);

    if (m_socket_fd == -1)
    {
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
    if (this != &other)
    {
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
    if (m_socket_fd != -1)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        m_is_connected = false;
        m_is_listening = false;
    }
}

void SockCppWrap::bind(const std::string& address, int port)
{
    validate_socket_state();

    if (m_is_connected || m_is_listening)
    {
        throw NetworkException("Cannot bind: socket already in use", EINVAL);
    }

    struct sockaddr_storage storage;
    socklen_t length = 0;

    if (!make_address(address, port, storage, length))
    {
        throw NetworkException("Invalid address: " + address, EINVAL);
    }

    check_error(
        ::bind(
            m_socket_fd,
            reinterpret_cast<struct sockaddr*>(&storage),
            length),
        "bind() failed");
}

void SockCppWrap::listen(int backlog)
{
    validate_socket_state();

    if (m_type != SocketType::STREAM)
    {
        throw NetworkException("listen() can only be called on stream sockets", EINVAL);
    }

    if (backlog < 0)
    {
        backlog = 0;
    }

    check_error(::listen(m_socket_fd, backlog), "listen() failed");
    m_is_listening = true;
}

SockCppWrap SockCppWrap::accept()
{
    validate_socket_state();

    if (!m_is_listening)
    {
        throw NetworkException("Socket must be listening before accept()", EINVAL);
    }

    struct sockaddr_storage client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(
        m_socket_fd,
        reinterpret_cast<struct sockaddr*>(&client_addr),
        &addr_len);

    if (client_fd == -1)
    {
        throw NetworkException("accept() failed", errno);
    }

    return SockCppWrap(AdoptFdTag{}, client_fd, m_family, m_type);
}

std::optional<SockCppWrap> SockCppWrap::try_accept()
{
    validate_socket_state();

    if (!m_is_listening)
    {
        throw NetworkException("Socket must be listening before accept()", EINVAL);
    }

    struct sockaddr_storage client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    for (;;)
    {
        int client_fd = ::accept(
            m_socket_fd,
            reinterpret_cast<struct sockaddr*>(&client_addr),
            &addr_len);

        if (client_fd != -1)
        {
            return SockCppWrap(AdoptFdTag{}, client_fd, m_family, m_type);
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return std::nullopt;
        }

        if (errno == EINTR || errno == ECONNABORTED)
        {
            continue;
        }

        throw NetworkException("accept() failed", errno);
    }
}

void SockCppWrap::connect(const std::string& address, int port)
{
    validate_socket_state();

    if (m_is_connected)
    {
        throw NetworkException("Socket already connected", EISCONN);
    }

    struct sockaddr_storage storage;
    socklen_t length = 0;

    if (!make_address(address, port, storage, length))
    {
        throw NetworkException("Invalid address: " + address, EINVAL);
    }

    check_error(
        ::connect(
            m_socket_fd,
            reinterpret_cast<struct sockaddr*>(&storage),
            length),
        "connect() failed");

    m_is_connected = true;
}

size_t SockCppWrap::send(const std::string& data, int flags)
{
    return send(data.data(), data.size(), flags);
}

size_t SockCppWrap::send(const void* data, size_t length, int flags)
{
    validate_socket_state();

    if (!m_is_connected)
    {
        throw NetworkException("Socket not connected", ENOTCONN);
    }

    if (length == 0)
    {
        return 0;
    }

    ssize_t sent = ::send(m_socket_fd, data, length, flags | MSG_NOSIGNAL);

    if (sent == -1)
    {
        throw NetworkException("send() failed", errno);
    }

    return static_cast<size_t>(sent);
}

std::string SockCppWrap::recv(size_t buffer_size, int flags)
{
    if (buffer_size == 0)
    {
        return std::string();
    }

    std::string buffer(buffer_size, '\0');
    size_t received = recv(buffer.data(), buffer_size, flags);
    buffer.resize(received);

    return buffer;
}

size_t SockCppWrap::recv(void* buffer, size_t buffer_size, int flags)
{
    validate_socket_state();

    if (!m_is_connected)
    {
        throw NetworkException("Socket not connected", ENOTCONN);
    }

    if (buffer_size == 0)
    {
        return 0;
    }

    ssize_t received = ::recv(m_socket_fd, buffer, buffer_size, flags);

    if (received == -1)
    {
        throw NetworkException("recv() failed", errno);
    }

    if (received == 0)
    {
        m_is_connected = false;
    }

    return static_cast<size_t>(received);
}

IoResult SockCppWrap::recv_nb(void* buffer,
                              size_t buffer_size,
                              size_t& bytes_received,
                              int flags)
{
    validate_socket_state();

    bytes_received = 0;

    if (buffer_size == 0)
    {
        return IoResult::OK;
    }

    ssize_t received;

    do
    {
        received = ::recv(m_socket_fd, buffer, buffer_size, flags);
    }
    while (received == -1 && errno == EINTR);

    if (received == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return IoResult::WOULD_BLOCK;
        }

        throw NetworkException("recv() failed", errno);
    }

    if (received == 0)
    {
        m_is_connected = false;
        return IoResult::CLOSED;
    }

    bytes_received = static_cast<size_t>(received);
    return IoResult::OK;
}

IoResult SockCppWrap::send_nb(const void* data,
                              size_t length,
                              size_t& bytes_sent,
                              int flags)
{
    validate_socket_state();

    bytes_sent = 0;

    if (length == 0)
    {
        return IoResult::OK;
    }

    ssize_t sent;

    do
    {
        sent = ::send(m_socket_fd, data, length, flags | MSG_NOSIGNAL);
    }
    while (sent == -1 && errno == EINTR);

    if (sent == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return IoResult::WOULD_BLOCK;
        }

        throw NetworkException("send() failed", errno);
    }

    bytes_sent = static_cast<size_t>(sent);
    return IoResult::OK;
}

size_t SockCppWrap::send_to(const void* data,
                            size_t length,
                            const std::string& address,
                            int port,
                            int flags)
{
    validate_socket_state();

    struct sockaddr_storage storage;
    socklen_t storage_length = 0;

    if (!make_address(address, port, storage, storage_length))
    {
        throw NetworkException("Invalid address: " + address, EINVAL);
    }

    ssize_t sent;

    do
    {
        sent = ::sendto(
            m_socket_fd,
            data,
            length,
            flags,
            reinterpret_cast<struct sockaddr*>(&storage),
            storage_length);
    }
    while (sent == -1 && errno == EINTR);

    if (sent == -1)
    {
        throw NetworkException("sendto() failed", errno);
    }

    return static_cast<size_t>(sent);
}

size_t SockCppWrap::recv_from(void* buffer,
                              size_t buffer_size,
                              std::string& sender_address,
                              int& sender_port,
                              int flags)
{
    validate_socket_state();

    struct sockaddr_storage storage{};
    socklen_t storage_length = sizeof(storage);

    ssize_t received;

    do
    {
        received = ::recvfrom(
            m_socket_fd,
            buffer,
            buffer_size,
            flags,
            reinterpret_cast<struct sockaddr*>(&storage),
            &storage_length);
    }
    while (received == -1 && errno == EINTR);

    if (received == -1)
    {
        throw NetworkException("recvfrom() failed", errno);
    }

    auto formatted = format_address(storage);

    sender_address = std::move(formatted.first);
    sender_port = formatted.second;

    return static_cast<size_t>(received);
}

void SockCppWrap::set_nonblocking(bool non_blocking)
{
    validate_socket_state();

    int flags = ::fcntl(m_socket_fd, F_GETFL, 0);

    if (flags == -1)
    {
        throw NetworkException("fcntl(F_GETFL) failed", errno);
    }

    if (non_blocking)
    {
        flags |= O_NONBLOCK;
    }
    else
    {
        flags &= ~O_NONBLOCK;
    }

    check_error(::fcntl(m_socket_fd, F_SETFL, flags), "fcntl(F_SETFL) failed");
}

void SockCppWrap::set_reuse_address(bool reuse)
{
    validate_socket_state();

    int opt = reuse ? 1 : 0;

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)),
        "setsockopt(SO_REUSEADDR) failed");
}

void SockCppWrap::set_reuse_port(bool reuse)
{
    validate_socket_state();

#ifdef SO_REUSEPORT
    int opt = reuse ? 1 : 0;

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_REUSEPORT,
            &opt,
            sizeof(opt)),
        "setsockopt(SO_REUSEPORT) failed");
#else
    (void)reuse;
    throw NetworkException("SO_REUSEPORT is not supported on this platform", ENOPROTOOPT);
#endif
}

void SockCppWrap::set_send_timeout(std::chrono::milliseconds timeout)
{
    validate_socket_state();

    if (timeout < std::chrono::milliseconds::zero())
    {
        timeout = std::chrono::milliseconds::zero();
    }

    struct timeval tv;

    const auto ms = timeout.count();

    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &tv,
            sizeof(tv)),
        "setsockopt(SO_SNDTIMEO) failed");
}

void SockCppWrap::set_receive_timeout(std::chrono::milliseconds timeout)
{
    validate_socket_state();

    if (timeout < std::chrono::milliseconds::zero())
    {
        timeout = std::chrono::milliseconds::zero();
    }

    struct timeval tv;

    const auto ms = timeout.count();

    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)),
        "setsockopt(SO_RCVTIMEO) failed");
}

void SockCppWrap::set_nodelay(bool nodelay)
{
    validate_socket_state();

    int opt = nodelay ? 1 : 0;

    check_error(
        ::setsockopt(
            m_socket_fd,
            IPPROTO_TCP,
            TCP_NODELAY,
            &opt,
            sizeof(opt)),
        "setsockopt(TCP_NODELAY) failed");
}

void SockCppWrap::set_keepalive(bool keepalive)
{
    validate_socket_state();

    int opt = keepalive ? 1 : 0;

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_KEEPALIVE,
            &opt,
            sizeof(opt)),
        "setsockopt(SO_KEEPALIVE) failed");
}

void SockCppWrap::set_send_buffer_size(size_t size)
{
    validate_socket_state();

    if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw NetworkException("Send buffer size is too large", EINVAL);
    }

    int opt = static_cast<int>(size);

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_SNDBUF,
            &opt,
            sizeof(opt)),
        "setsockopt(SO_SNDBUF) failed");
}

void SockCppWrap::set_receive_buffer_size(size_t size)
{
    validate_socket_state();

    if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw NetworkException("Receive buffer size is too large", EINVAL);
    }

    int opt = static_cast<int>(size);

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_RCVBUF,
            &opt,
            sizeof(opt)),
        "setsockopt(SO_RCVBUF) failed");
}

void SockCppWrap::set_linger(bool enable, int seconds)
{
    validate_socket_state();

    struct linger value;

    value.l_onoff = enable ? 1 : 0;
    value.l_linger = seconds;

    check_error(
        ::setsockopt(
            m_socket_fd,
            SOL_SOCKET,
            SO_LINGER,
            &value,
            sizeof(value)),
        "setsockopt(SO_LINGER) failed");
}

void SockCppWrap::shutdown(ShutdownHow how)
{
    if (m_socket_fd != -1)
    {
        ::shutdown(m_socket_fd, static_cast<int>(how));
    }
}

std::pair<std::string, int> SockCppWrap::get_local_address() const
{
    validate_socket_state();

    struct sockaddr_storage storage{};
    socklen_t length = sizeof(storage);

    check_error(
        ::getsockname(
            m_socket_fd,
            reinterpret_cast<struct sockaddr*>(&storage),
            &length),
        "getsockname() failed");

    return format_address(storage);
}

std::pair<std::string, int> SockCppWrap::get_remote_address() const
{
    validate_socket_state();

    struct sockaddr_storage storage{};
    socklen_t length = sizeof(storage);

    check_error(
        ::getpeername(
            m_socket_fd,
            reinterpret_cast<struct sockaddr*>(&storage),
            &length),
        "getpeername() failed");

    return format_address(storage);
}

bool SockCppWrap::is_valid_ipv4(const std::string& address)
{
    struct in_addr addr;
    return ::inet_pton(AF_INET, address.c_str(), &addr) == 1;
}

bool SockCppWrap::is_valid_ipv6(const std::string& address)
{
    struct in6_addr addr;
    return ::inet_pton(AF_INET6, address.c_str(), &addr) == 1;
}

} // namespace Simple