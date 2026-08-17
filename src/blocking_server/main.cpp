#include <iostream>
#include <thread>
#include <utility>

#include "sockcppwrap/sockcppwrap.h"

int main(int argc, char* argv[])
{
    int server_port = 6767;
    std::string server_ip = "127.0.0.1"; // "0.0.0.0" is INNADDR_ANY

    try {
        Simple::SockCppWrap server_socket(
            Simple::AddressFamily::INET,
            Simple::SocketType::STREAM
        );

        server_socket.set_reuse_address(true);
        server_socket.bind(server_ip, server_port);
        server_socket.listen(64);

        std::cout << "server: listening on port " << server_port << std::endl;
        
        while (true) {
            Simple::SockCppWrap client_socket;

            try {
                client_socket = server_socket.accept();
            }
            catch (const Simple::NetworkException& e) {
                std::cerr << "server accept() error: " << e.what() << " Retrying..." << std::endl;
                continue;
            }

            std::thread([client_socket = std::move(client_socket)]() mutable {
                auto [client_ip, client_port] = client_socket.get_remote_address();
                std::cout << "server: got connection from " << client_ip + ":" + std::to_string(client_port) << std::endl;

                try {
                    while (true) {
                        std::string received_data = client_socket.recv(1024);
                        
                        if (received_data.empty()) {
                            break;
                        }
                        
                        std::cout << "server received: " << received_data << std::endl;
                        
                        client_socket.send(received_data);
                    }
                } catch (const Simple::NetworkException& e) {
                    std::cout << "client error: " << e.what() << std::endl;
                }

                std::cout << "server: client " << client_ip << ":" << std::to_string(client_port) << " disconnected" << std::endl;
            }).detach();
        }
    }
    catch (const Simple::NetworkException& e) {
        std::cerr << "server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}