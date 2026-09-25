#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_map>

#include "connection.h"

class Server
{
public:
    Server(int port, const std::string& docroot);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool init();

    void run();

private:

    void accept_clients();

    void handle_read(int cfd);


    void handle_write(int cfd);


    void process_request(int cfd);

    void start_write(int cfd, const std::string& data);


    void close_connection(int cfd);


    void sweep_idle();


    bool epoll_mod(int cfd, std::uint32_t events);


    Connection* find_conn(int cfd);

    static bool set_nonblocking(int fd);
    static void raise_fd_limit();



    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int port_ = 0;
    std::string docroot_;
    std::time_t last_sweep_ = 0;

    std::unordered_map<int, Connection> conns_;


    static constexpr int kMaxEvents = 64;
    static constexpr int kEpollTimeoutMs = 1000;
    static constexpr std::time_t kIdleSeconds = 30;
    static constexpr std::size_t kMaxRequestSize = 8192;
};
