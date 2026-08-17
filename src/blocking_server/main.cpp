#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <csignal>
#include <vector>
#include <chrono>
#include <utility>
#include "sockcppwrap/sockcppwrap.h"

static std::atomic<bool> g_running{true};
static std::mutex g_console_mutex;

void signal_handler(int) {
    g_running.store(false);
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int server_port = 6767;
    std::string server_ip = "127.0.0.1";

    std::vector<std::thread> client_threads;

    try {
        Simple::SockCppWrap server_socket(
            Simple::AddressFamily::INET,
            Simple::SocketType::STREAM
        );

        server_socket.set_reuse_address(true);
        server_socket.bind(server_ip, server_port);
        server_socket.listen(64);

        // Set a short receive timeout so accept() doesn't block forever
        // This allows the loop to check g_running periodically
        server_socket.set_receive_timeout(std::chrono::milliseconds(500));

        {
            std::lock_guard<std::mutex> lock(g_console_mutex);
            std::cout << "server: listening on port " << server_port << std::endl;
        }

        while (g_running.load()) {
            Simple::SockCppWrap client_socket;

            try {
                client_socket = server_socket.accept();
            } catch (const Simple::NetworkException& e) {
                // EAGAIN/EWOULDBLOCK means timeout expired, no connection pending
                if (e.error_code() == EAGAIN || e.error_code() == EWOULDBLOCK) {
                    continue; // Check g_running and retry
                }
                if (!g_running.load()) break;
                std::lock_guard<std::mutex> lock(g_console_mutex);
                std::cerr << "server accept() error: " << e.what() << " Retrying..." << std::endl;
                continue;
            }

            client_threads.emplace_back([client_socket = std::move(client_socket)]() mutable {
                auto [client_ip, client_port] = client_socket.get_remote_address();

                {
                    std::lock_guard<std::mutex> lock(g_console_mutex);
                    std::cout << "server: got connection from "
                              << client_ip << ":" << client_port << std::endl;
                }

                try {
                    while (g_running.load()) {
                        std::string received_data = client_socket.recv(1024);

                        if (received_data.empty()) {
                            break;
                        }

                        {
                            std::lock_guard<std::mutex> lock(g_console_mutex);
                            std::cout << "server received: " << received_data << std::endl;
                        }

                        size_t total_sent = 0;
                        while (total_sent < received_data.size()) {
                            size_t sent = client_socket.send(
                                received_data.data() + total_sent,
                                received_data.size() - total_sent
                            );
                            total_sent += sent;
                        }
                    }
                } catch (const Simple::NetworkException& e) {
                    std::lock_guard<std::mutex> lock(g_console_mutex);
                    std::cout << "client error: " << e.what() << std::endl;
                }

                {
                    std::lock_guard<std::mutex> lock(g_console_mutex);
                    std::cout << "server: client " << client_ip << ":"
                              << client_port << " disconnected" << std::endl;
                }
            });
        }

    } catch (const Simple::NetworkException& e) {
        std::lock_guard<std::mutex> lock(g_console_mutex);
        std::cerr << "server error: " << e.what() << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(g_console_mutex);
        std::cout << "server: shutting down, waiting for "
                  << client_threads.size() << " client(s)..." << std::endl;
    }

    for (auto& t : client_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_console_mutex);
        std::cout << "server: shutdown complete" << std::endl;
    }

    return 0;
}