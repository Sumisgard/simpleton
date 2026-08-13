#pragma once

#include <string>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

namespace Simple
{
namespace Network
{

class NetworkException : public std::runtime_error
{
private:
    int m_errno_code;

public:
    explicit NetworkException(const std::string& message)
        : std::runtime_error(message + ": " + std::strerror(errno))
        , m_errno_code(errno) {}

    explicit NetworkException(const std::string& message, int err) 
        : std::runtime_error(message + ": " + std::strerror(err))
        , m_errno_code(err) {}

    int error_code() const { return m_errno_code; }
};

enum class SocketType {
    STREAM = SOCK_STREAM,
    DATAGRAM = SOCK_DGRAM,
    SEQPACKET = SOCK_SEQPACKET
};

enum class AddressFamily {
    INET = AF_INET,
    INET6 = AF_INET6,
    UNIX = AF_UNIX
};

enum class ShutdownHow {
    READ = SHUT_RD,
    WRITE = SHUT_WR,
    BOTH = SHUT_RDWR
};

class SockCppWrap
{
private:
    int m_socket_fd;
    bool m_is_connected;
    bool m_is_listening;
    AddressFamily m_family;
    SocketType m_type;

    void check_error(int result, const std::string& operation) const;
    void validate_socket_state() const;

public:
    SockCppWrap(AddressFamily family = AddressFamily::INET, SocketType type = SocketType::STREAM, int protocol = 0);
    ~SockCppWrap();

    SockCppWrap(const SockCppWrap&) = delete;
    SockCppWrap& operator=(const SockCppWrap&) = delete;
    
    SockCppWrap(SockCppWrap&& other) noexcept;
    SockCppWrap& operator=(SockCppWrap&& other) noexcept;

    void close();
    void bind(const std::string& address, int port);
    void listen(int backlog = 5);
    SockCppWrap accept();
    void connect(const std::string& address, int port);
    
    size_t send(const std::string& data, int flags = 0);
    size_t send(const void* data, size_t length, int flags = 0);
    std::string recv(size_t buffer_size = 4096, int flags = 0);
    size_t recv(void* buffer, size_t buffer_size, int flags = 0);
    
    size_t send_to(const void* data, size_t length, const std::string& address, int port, int flags = 0);
    size_t recv_from(void* buffer, size_t buffer_size, std::string& sender_address, int& sender_port, int flags = 0);

    void set_nonblocking(bool non_blocking);
    void set_reuse_address(bool reuse);
    void set_reuse_port(bool reuse);
    void set_send_timeout(std::chrono::milliseconds timeout);
    void set_receive_timeout(std::chrono::milliseconds timeout);
    void set_nodelay(bool nodelay);
    void set_keepalive(bool keepalive);
    void set_send_buffer_size(size_t size);
    void set_receive_buffer_size(size_t size);
    void set_linger(bool enable, int seconds);

    int get_socket_fd() const { return m_socket_fd; }
    bool is_valid() const { return m_socket_fd != -1; }
    bool connected() const { return m_is_connected; }
    bool listening() const { return m_is_listening; }
    AddressFamily address_family() const { return m_family; }
    SocketType socket_type() const { return m_type; }
    
    void shutdown(ShutdownHow how = ShutdownHow::BOTH);
    std::pair<std::string, int> get_local_address() const;
    std::pair<std::string, int> get_remote_address() const;

    // Utility functions
    static bool is_valid_ipv4(const std::string& address);
    static bool is_valid_ipv6(const std::string& address);
};

// RAII wrapper for automatic cleanup
class SocketGuard {
private:
    SockCppWrap* m_socket;

public:
    explicit SocketGuard(SockCppWrap* socket) : m_socket(socket) {}
    ~SocketGuard() { if (m_socket && m_socket->is_valid()) m_socket->close(); }
    void release() { m_socket = nullptr; }
};

} // namespace Network
} // namespace Simple