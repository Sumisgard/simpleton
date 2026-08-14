#include <iostream>

#include "sockcppwrap/sockcppwrap.h"

int main(int argc, char* argv[])
{
    int server_port = 6767;
    std::string server_ip = "127.0.0.1"; // "0.0.0.0" is INNADDR_ANY

    try {
        Simple::Network::SockCppWrap server_socket(
            Simple::Network::AddressFamily::INET,
            Simple::Network::SocketType::STREAM
        );

        server_socket.set_reuse_address(true);
        server_socket.bind(server_ip, server_port);
        server_socket.listen(10);

        std::cout << "server: listening on port " << server_port << std::endl;
        
        while (true) {
            Simple::Network::SockCppWrap client_socket;

            try {
                client_socket = server_socket.accept();
            }
            catch (const Simple::Network::NetworkException& e) {
                std::cerr << "server accept() error: " << e.what() << ". Retrying..." << std::endl;
                continue;
            }

            auto [client_ip, client_port] = client_socket.get_local_address();
            std::cout << "server: got connection from " << client_ip + ":" + std::to_string(client_port) << std::endl;
            client_socket.send("Hello, world!");
        }
    }
    catch (const Simple::Network::NetworkException& e) {
        std::cerr << "server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}