// epoll_echo_server.cpp
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static std::atomic<bool> g_running{true};
static std::mutex g_console_mutex;

void signal_handler(int) {
    g_running.store(false);
}

// Per-connection state for edge-triggered epoll
struct ConnectionState {
    int fd;
    std::string read_buffer;
    std::string write_buffer;
    size_t write_offset;
    bool closed;

    explicit ConnectionState(int socket_fd)
        : fd(socket_fd), write_offset(0), closed(false) {}
};

static void log_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_console_mutex);
    std::cout << msg << std::endl;
}

static void log_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_console_mutex);
    std::cerr << msg << std::endl;
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_error("fcntl(F_GETFL) failed: " + std::string(strerror(errno)));
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("fcntl(F_SETFL, O_NONBLOCK) failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

static void close_connection(int epfd, int fd,
                             std::unordered_map<int, ConnectionState>& connections) {
    auto it = connections.find(fd);
    if (it != connections.end()) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        connections.erase(it);
    }
}

// Edge-triggered: MUST drain all available data in a loop until EAGAIN
static void handle_read(int epfd, ConnectionState& conn,
                        std::unordered_map<int, ConnectionState>& connections) {
    char buf[4096];

    while (true) {
        ssize_t n = ::recv(conn.fd, buf, sizeof(buf), 0);

        if (n > 0) {
            conn.read_buffer.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            // Peer performed orderly shutdown
            log_message("client fd=" + std::to_string(conn.fd) + " disconnected");
            close_connection(epfd, conn.fd, connections);
            return;
        } else {
            // n == -1
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All available data has been read; this is expected in ET mode
                break;
            }
            log_error("recv() error on fd=" + std::to_string(conn.fd) +
                      ": " + strerror(errno));
            close_connection(epfd, conn.fd, connections);
            return;
        }
    }

    // Echo: move everything from read_buffer to write_buffer
    if (!conn.read_buffer.empty()) {
        conn.write_buffer += conn.read_buffer;
        conn.read_buffer.clear();

        // Register for EPOLLOUT if we weren't already
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = conn.fd;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn.fd, &ev) == -1) {
            log_error("epoll_ctl(MOD, EPOLLOUT) failed: " + std::string(strerror(errno)));
            close_connection(epfd, conn.fd, connections);
        }
    }
}

// Edge-triggered: MUST write as much as possible in a loop until EAGAIN
static void handle_write(int epfd, ConnectionState& conn,
                         std::unordered_map<int, ConnectionState>& connections) {
    while (conn.write_offset < conn.write_buffer.size()) {
        ssize_t n = ::send(conn.fd,
                           conn.write_buffer.data() + conn.write_offset,
                           conn.write_buffer.size() - conn.write_offset,
                           MSG_NOSIGNAL);

        if (n > 0) {
            conn.write_offset += static_cast<size_t>(n);
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel send buffer full; stop writing, wait for next EPOLLOUT
                break;
            }
            log_error("send() error on fd=" + std::to_string(conn.fd) +
                      ": " + strerror(errno));
            close_connection(epfd, conn.fd, connections);
            return;
        }
    }

    // If all data was written, compact the buffer and stop watching EPOLLOUT
    if (conn.write_offset >= conn.write_buffer.size()) {
        conn.write_buffer.clear();
        conn.write_offset = 0;

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = conn.fd;
        if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn.fd, &ev) == -1) {
            log_error("epoll_ctl(MOD, ~EPOLLOUT) failed: " + std::string(strerror(errno)));
            close_connection(epfd, conn.fd, connections);
        }
    }
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const int server_port = 6767;
    const std::string server_ip = "127.0.0.1";
    const int max_events = 64;

    // Create listening socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        log_error("socket() failed: " + std::string(strerror(errno)));
        return 1;
    }

    int opt = 1;
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        log_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    if (::inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr) <= 0) {
        log_error("Invalid address: " + server_ip);
        ::close(listen_fd);
        return 1;
    }

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        log_error("bind() failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 64) == -1) {
        log_error("listen() failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return 1;
    }

    if (!set_nonblocking(listen_fd)) {
        ::close(listen_fd);
        return 1;
    }

    // Create epoll instance
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) {
        log_error("epoll_create1() failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return 1;
    }

    // Add listening socket to epoll (edge-triggered)
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        log_error("epoll_ctl(ADD, listen_fd) failed: " + std::string(strerror(errno)));
        ::close(epfd);
        ::close(listen_fd);
        return 1;
    }

    log_message("server: listening on " + server_ip + ":" + std::to_string(server_port) +
                " (epoll ET mode)");

    std::unordered_map<int, ConnectionState> connections;
    std::vector<struct epoll_event> events(max_events);

    while (g_running.load()) {
        int nfds = ::epoll_wait(epfd, events.data(), max_events, 500);

        if (nfds == -1) {
            if (errno == EINTR) continue; // Signal interrupted, check g_running
            log_error("epoll_wait() failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                // Edge-triggered accept: MUST loop until EAGAIN
                while (true) {
                    struct sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = ::accept(listen_fd,
                                             reinterpret_cast<struct sockaddr*>(&client_addr),
                                             &addr_len);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // No more pending connections
                        }
                        log_error("accept() failed: " + std::string(strerror(errno)));
                        break;
                    }

                    if (!set_nonblocking(client_fd)) {
                        ::close(client_fd);
                        continue;
                    }

                    // Disable Nagle's algorithm
                    int nodelay = 1;
                    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                                 &nodelay, sizeof(nodelay));

                    // Add to epoll as edge-triggered, read-only initially
                    struct epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLET;
                    cev.data.fd = client_fd;
                    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) == -1) {
                        log_error("epoll_ctl(ADD, client) failed: " + std::string(strerror(errno)));
                        ::close(client_fd);
                        continue;
                    }

                    connections.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(client_fd),
                                        std::forward_as_tuple(client_fd));

                    char ip_str[INET_ADDRSTRLEN];
                    ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    log_message("server: accepted connection from " +
                                std::string(ip_str) + ":" +
                                std::to_string(ntohs(client_addr.sin_port)) +
                                " fd=" + std::to_string(client_fd));
                }
            } else {
                // Client socket event
                auto it = connections.find(fd);
                if (it == connections.end()) continue;

                ConnectionState& conn = it->second;

                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    log_message("client fd=" + std::to_string(fd) + " error/hangup");
                    close_connection(epfd, fd, connections);
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    handle_read(epfd, conn, connections);
                    // Connection may have been removed; re-check iterator validity
                    if (connections.find(fd) == connections.end()) continue;
                }

                if (events[i].events & EPOLLOUT) {
                    // Re-find since handle_read may have modified the map
                    auto wit = connections.find(fd);
                    if (wit != connections.end()) {
                        handle_write(epfd, wit->second, connections);
                    }
                }
            }
        }
    }

    log_message("server: shutting down...");

    // Clean up all connections
    for (auto& [fd, conn] : connections) {
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
    }
    connections.clear();

    ::close(epfd);
    ::close(listen_fd);

    log_message("server: shutdown complete");
    return 0;
}