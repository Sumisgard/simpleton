#include "epoll_echo_server.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <unistd.h>

namespace Echo
{

// ---------------------------------------------------------------------------
// Epoll
// ---------------------------------------------------------------------------

Epoll::Epoll()
{
    m_fd = ::epoll_create1(EPOLL_CLOEXEC);

    if (m_fd == -1)
    {
        throw std::runtime_error(
            std::string("epoll_create1() failed: ") + std::strerror(errno));
    }
}

Epoll::~Epoll()
{
    if (m_fd != -1)
    {
        ::close(m_fd);
    }
}

Epoll::Epoll(Epoll&& other) noexcept
    : m_fd(other.m_fd)
{
    other.m_fd = -1;
}

Epoll& Epoll::operator=(Epoll&& other) noexcept
{
    if (this != &other)
    {
        if (m_fd != -1)
        {
            ::close(m_fd);
        }

        m_fd = other.m_fd;
        other.m_fd = -1;
    }

    return *this;
}

void Epoll::add(int fd, std::uint32_t events)
{
    struct epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (::epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        throw std::runtime_error(
            std::string("epoll_ctl(EPOLL_CTL_ADD) failed: ") + std::strerror(errno));
    }
}

void Epoll::modify(int fd, std::uint32_t events)
{
    struct epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (::epoll_ctl(m_fd, EPOLL_CTL_MOD, fd, &event) == -1)
    {
        throw std::runtime_error(
            std::string("epoll_ctl(EPOLL_CTL_MOD) failed: ") + std::strerror(errno));
    }
}

void Epoll::remove(int fd)
{
    ::epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, nullptr);
}

int Epoll::wait(struct epoll_event* events, int max_events, int timeout_ms)
{
    int count = ::epoll_wait(m_fd, events, max_events, timeout_ms);

    if (count == -1)
    {
        if (errno == EINTR)
        {
            return -1;
        }

        throw std::runtime_error(
            std::string("epoll_wait() failed: ") + std::strerror(errno));
    }

    return count;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

Connection::Connection(Simple::SockCppWrap socket)
    : m_socket(std::move(socket))
{
}

void Connection::readable()
{
    constexpr std::size_t kReadBufferSize = 4096;
    char buffer[kReadBufferSize];

    for (;;)
    {
        std::size_t bytes = 0;

        Simple::IoResult result = m_socket.recv_nb(
            buffer,
            sizeof(buffer),
            bytes);

        if (result == Simple::IoResult::OK)
        {
            if (bytes > 0)
            {
                m_read_buffer.append(buffer, bytes);
            }
        }
        else if (result == Simple::IoResult::CLOSED)
        {
            m_peer_closed = true;
            break;
        }
        else
        {
            break;
        }
    }

    transfer_read_to_write();
}

void Connection::writable()
{
    while (has_pending_write())
    {
        std::size_t sent = 0;

        Simple::IoResult result = m_socket.send_nb(
            m_write_buffer.data() + m_write_offset,
            m_write_buffer.size() - m_write_offset,
            sent);

        if (result == Simple::IoResult::OK)
        {
            if (sent == 0)
            {
                break;
            }

            m_write_offset += sent;
        }
        else if (result == Simple::IoResult::WOULD_BLOCK)
        {
            break;
        }
        else
        {
            break;
        }
    }

    if (!has_pending_write())
    {
        m_write_buffer.clear();
        m_write_offset = 0;
    }
}

bool Connection::has_pending_write() const
{
    return m_write_offset < m_write_buffer.size();
}

bool Connection::peer_closed() const
{
    return m_peer_closed;
}

int Connection::fd() const
{
    return m_socket.get_socket_fd();
}

std::uint32_t Connection::interest() const
{
    return m_interest;
}

void Connection::set_interest(std::uint32_t interest)
{
    m_interest = interest;
}

std::string Connection::describe() const
{
    try
    {
        auto [ip, port] = m_socket.get_remote_address();
        return ip + ":" + std::to_string(port);
    }
    catch (...)
    {
        return "fd=" + std::to_string(fd());
    }
}

void Connection::transfer_read_to_write()
{
    if (!m_read_buffer.empty())
    {
        m_write_buffer.append(m_read_buffer);
        m_read_buffer.clear();
    }
}

// ---------------------------------------------------------------------------
// EchoServer
// ---------------------------------------------------------------------------

EchoServer::EchoServer(std::string ip, std::uint16_t port)
    : m_ip(std::move(ip))
    , m_port(port)
    , m_listener(Simple::AddressFamily::INET, Simple::SocketType::STREAM)
{
    m_listener.set_reuse_address(true);
    m_listener.bind(m_ip, m_port);
    m_listener.listen(64);
    m_listener.set_nonblocking(true);

    m_epoll.add(
        m_listener.get_socket_fd(),
        static_cast<std::uint32_t>(EPOLLIN | EPOLLET));
}

void EchoServer::run(std::atomic<bool>& running)
{
    constexpr int kMaxEvents = 64;

    std::vector<struct epoll_event> events(kMaxEvents);

    log("server: listening on " + m_ip + ":" + std::to_string(m_port) +
        " using epoll edge-triggered mode");

    while (running.load())
    {
        int count = m_epoll.wait(
            events.data(),
            static_cast<int>(events.size()),
            500);

        if (count < 0)
        {
            continue;
        }

        for (int i = 0; i < count; ++i)
        {
            int fd = events[i].data.fd;
            std::uint32_t event_mask = events[i].events;

            try
            {
                if (fd == m_listener.get_socket_fd())
                {
                    accept_connections();
                }
                else
                {
                    handle_client_event(fd, event_mask);
                }
            }
            catch (const std::exception& e)
            {
                log(std::string("server: event processing error: ") + e.what());

                if (fd != m_listener.get_socket_fd())
                {
                    close_connection(fd);
                }
            }
            catch (...)
            {
                log("server: unknown event processing error");

                if (fd != m_listener.get_socket_fd())
                {
                    close_connection(fd);
                }
            }
        }
    }

    log("server: shutting down...");

    for (auto& [fd, connection] : m_connections)
    {
        static_cast<void>(connection);
        m_epoll.remove(fd);
    }

    m_connections.clear();

    log("server: shutdown complete");
}

void EchoServer::accept_connections()
{
    for (;;)
    {
        std::optional<Simple::SockCppWrap> maybe_client;

        try
        {
            maybe_client = m_listener.try_accept();
        }
        catch (const Simple::NetworkException& e)
        {
            log(std::string("server: accept error: ") + e.what());
            break;
        }

        if (!maybe_client.has_value())
        {
            break;
        }

        Simple::SockCppWrap client = std::move(*maybe_client);

        try
        {
            client.set_nonblocking(true);
            client.set_nodelay(true);
        }
        catch (const Simple::NetworkException& e)
        {
            log(std::string("server: failed to configure accepted socket: ") + e.what());
            continue;
        }

        int fd = client.get_socket_fd();

        if (m_connections.find(fd) != m_connections.end())
        {
            log("server: duplicate descriptor after accept: fd=" + std::to_string(fd));
            continue;
        }

        auto [it, inserted] = m_connections.try_emplace(fd, std::move(client));

        if (!inserted)
        {
            log("server: failed to insert connection state: fd=" + std::to_string(fd));
            continue;
        }

        try
        {
            m_epoll.add(fd, static_cast<std::uint32_t>(EPOLLIN | EPOLLET));
        }
        catch (const std::exception& e)
        {
            log(std::string("server: failed to add client to epoll: ") + e.what());
            m_connections.erase(it);
            continue;
        }

        it->second.set_interest(static_cast<std::uint32_t>(EPOLLIN | EPOLLET));

        log("server: accepted " + it->second.describe() +
            " fd=" + std::to_string(fd));
    }
}

void EchoServer::handle_client_event(int fd, std::uint32_t event_mask)
{
    auto it = m_connections.find(fd);

    if (it == m_connections.end())
    {
        return;
    }

    Connection& connection = it->second;

    const std::uint32_t error_mask =
        static_cast<std::uint32_t>(EPOLLERR) |
        static_cast<std::uint32_t>(EPOLLHUP);

    if ((event_mask & error_mask) != 0)
    {
        log("server: " + connection.describe() + " error or hangup");
        close_connection(fd);
        return;
    }

    if ((event_mask & static_cast<std::uint32_t>(EPOLLIN)) != 0)
    {
        connection.readable();
    }

    if (connection.has_pending_write())
    {
        connection.writable();
    }

    if (connection.peer_closed() && !connection.has_pending_write())
    {
        log("server: " + connection.describe() + " disconnected");
        close_connection(fd);
        return;
    }

    update_interest(connection);
}

void EchoServer::update_interest(Connection& connection)
{
    std::uint32_t events = static_cast<std::uint32_t>(EPOLLET);

    if (!connection.peer_closed())
    {
        events |= static_cast<std::uint32_t>(EPOLLIN);
    }

    if (connection.has_pending_write())
    {
        events |= static_cast<std::uint32_t>(EPOLLOUT);
    }

    if (events != connection.interest())
    {
        m_epoll.modify(connection.fd(), events);
        connection.set_interest(events);
    }
}

void EchoServer::close_connection(int fd)
{
    auto it = m_connections.find(fd);

    if (it == m_connections.end())
    {
        return;
    }

    m_epoll.remove(fd);
    m_connections.erase(it);
}

void EchoServer::log(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_log_mutex);
    std::cout << message << std::endl;
}

} // namespace Echo