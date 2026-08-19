#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <sys/epoll.h>

#include "sockcppwrap/sockcppwrap.h"

namespace Echo
{

class Epoll
{
public:
    Epoll();
    ~Epoll();

    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;

    Epoll(Epoll&& other) noexcept;
    Epoll& operator=(Epoll&& other) noexcept;

    void add(int fd, std::uint32_t events);
    void modify(int fd, std::uint32_t events);
    void remove(int fd);

    int wait(struct epoll_event* events, int max_events, int timeout_ms);

private:
    int m_fd = -1;
};

class Connection
{
public:
    explicit Connection(Simple::SockCppWrap socket);

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void readable();
    void writable();

    bool has_pending_write() const;
    bool peer_closed() const;

    int fd() const;

    std::uint32_t interest() const;
    void set_interest(std::uint32_t interest);

    std::string describe() const;

private:
    void transfer_read_to_write();

    Simple::SockCppWrap m_socket;

    std::string m_read_buffer;
    std::string m_write_buffer;
    std::size_t m_write_offset = 0;

    bool m_peer_closed = false;
    std::uint32_t m_interest = 0;
};

class EchoServer
{
public:
    EchoServer(std::string ip, std::uint16_t port);

    EchoServer(const EchoServer&) = delete;
    EchoServer& operator=(const EchoServer&) = delete;

    void run(std::atomic<bool>& running);

private:
    void accept_connections();
    void handle_client_event(int fd, std::uint32_t event_mask);
    void update_interest(Connection& connection);
    void close_connection(int fd);
    void log(const std::string& message);

    std::string m_ip;
    std::uint16_t m_port;

    Simple::SockCppWrap m_listener;
    Epoll m_epoll;

    std::unordered_map<int, Connection> m_connections;
    std::mutex m_log_mutex;
};

} // namespace Echo