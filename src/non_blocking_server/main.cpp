#include "non_blocking_server/epoll_echo_server.h"

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>

namespace
{

std::atomic<bool> g_running{true};

void signal_handler(int)
{
    g_running.store(false);
}

} // namespace

int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try
    {
        Echo::EchoServer server("127.0.0.1", 6767);
        server.run(g_running);
    }
    catch (const std::exception& e)
    {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}