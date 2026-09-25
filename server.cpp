#include "server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <vector>

#include "files.h"
#include "http.h"

Server::Server(int port, const std::string& docroot)
    : port_(port), docroot_(docroot)
{
}

Server::~Server()
{

    for (const auto& entry : conns_)
    {
        ::close(entry.first);
    }
    conns_.clear();

    if (epoll_fd_ != -1)
    {
        ::close(epoll_fd_);
    }

    if (listen_fd_ != -1)
    {
        ::close(listen_fd_);
    }
}

bool Server::set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL");
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL");
        return false;
    }

    return true;
}

void Server::raise_fd_limit()
{
    struct rlimit rl = {};

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
    {
        return;
    }

    if (rl.rlim_cur < rl.rlim_max)
    {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
        getrlimit(RLIMIT_NOFILE, &rl);
    }

    printf("fd limit: %lu\n", static_cast<unsigned long>(rl.rlim_cur));
}

bool Server::init()
{
    if (port_ <= 0 || port_ > 65535)
    {
        fprintf(stderr, "invalid port: %d\n", port_);
        return false;
    }

    std::string root = canonical_dir(docroot_);
    if (root.empty())
    {
        fprintf(stderr, "docroot is not a directory: %s\n", docroot_.c_str());
        return false;
    }
    docroot_ = root;

    raise_fd_limit();

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1)
    {
        perror("socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt");
        return false;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr),
             sizeof(addr)) == -1)
    {
        perror("bind");
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) == -1)
    {
        perror("listen");
        return false;
    }

    if (!set_nonblocking(listen_fd_))
    {
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1)
    {
        perror("epoll_create1");
        return false;
    }

    epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) == -1)
    {
        perror("epoll_ctl ADD listen_fd");
        return false;
    }

    last_sweep_ = time(nullptr);

    printf("listening on http://localhost:%d/\n", port_);
    printf("docroot: %s\n", docroot_.c_str());

    return true;
}

Connection* Server::find_conn(int cfd)
{
    auto it = conns_.find(cfd);
    return (it == conns_.end()) ? nullptr : &it->second;
}

bool Server::epoll_mod(int cfd, std::uint32_t events)
{
    epoll_event ev = {};
    ev.events = events;
    ev.data.fd = cfd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, cfd, &ev) == -1)
    {
        perror("epoll_ctl MOD");
        return false;
    }

    return true;
}

void Server::close_connection(int cfd)
{
    if (find_conn(cfd) == nullptr)
    {
        return;
    }

    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, cfd, nullptr);
    ::close(cfd);
    conns_.erase(cfd);
}

void Server::run()
{
    std::vector<epoll_event> events(kMaxEvents);

    for (;;)
    {
        int ready = epoll_wait(epoll_fd_, events.data(),
                               static_cast<int>(events.size()),
                               kEpollTimeoutMs);

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        bool has_listen_event = false;

        for (int i = 0; i < ready; ++i)
        {
            int cfd = events[i].data.fd;

            if (cfd == listen_fd_)
            {
                has_listen_event = true;
                continue;
            }

            Connection* conn = find_conn(cfd);
            if (conn == nullptr)
            {
                continue;
            }

            std::uint32_t mask = events[i].events;

            if (mask & (EPOLLERR | EPOLLHUP))
            {
                close_connection(cfd);
                continue;
            }

            conn->last_active = time(nullptr);

            if (mask & EPOLLIN)
            {
                handle_read(cfd);
            }

            if (find_conn(cfd) != nullptr && (mask & EPOLLOUT))
            {
                handle_write(cfd);
            }
        }

        if (has_listen_event)
        {
            accept_clients();
        }

        std::time_t now = time(nullptr);
        if (now - last_sweep_ >= 1)
        {
            last_sweep_ = now;
            sweep_idle();
        }
    }
}

void Server::accept_clients()
{
    for (;;)
    {
        int cfd = accept(listen_fd_, nullptr, nullptr);

        if (cfd == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }

            if (errno == EMFILE || errno == ENFILE)
            {
                return;
            }

            perror("accept");
            return;
        }
        if (!set_nonblocking(cfd))
        {
            ::close(cfd);
            continue;
        }

        epoll_event ev = {};
        ev.events = EPOLLIN;
        ev.data.fd = cfd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev) == -1)
        {
            perror("epoll_ctl ADD client");
            ::close(cfd);
            continue;
        }

        Connection conn;
        conn.last_active = time(nullptr);
        conns_[cfd] = conn;
    }
}

void Server::handle_read(int cfd)
{
    Connection* conn = find_conn(cfd);
    if (conn == nullptr)
    {
        return;
    }

    char buf[4096];

    for (;;)
    {
        ssize_t n = read(cfd, buf, sizeof(buf));

        if (n > 0)
        {
            conn->read_buf.append(buf, static_cast<std::size_t>(n));

            if (conn->read_buf.size() > kMaxRequestSize)
            {
                start_write(cfd, make_error_response(431));
                return;
            }

            if (is_request_complete(conn->read_buf))
            {
                process_request(cfd);
                return;
            }
            continue;
        }

        if (n == 0)
        {
            close_connection(cfd);
            return;
        }

        if (errno == EINTR)
        {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return;
        }

        close_connection(cfd);
        return;
    }
}

void Server::process_request(int cfd)
{
    Connection* conn = find_conn(cfd);
    if (conn == nullptr)
    {
        return;
    }

    Request req = parse_request(conn->read_buf);

    conn->read_buf.clear();

    if (!req.valid)
    {
        start_write(cfd, make_error_response(400));
        return;
    }

    if (req.method != "GET" && req.method != "HEAD")
    {
        start_write(cfd, make_error_response(405));
        return;
    }

    std::string path = resolve_path(docroot_, req.target);
    if (path.empty())
    {
        start_write(cfd, make_error_response(404));
        return;
    }

    std::string body;
    if (!read_file(path, body))
    {
        start_write(cfd, make_error_response(500));
        return;
    }

    bool include_body = (req.method != "HEAD");
    start_write(cfd, build_response(200, mime_type(path), body, include_body));
}

void Server::start_write(int cfd, const std::string& data)
{
    Connection* conn = find_conn(cfd);
    if (conn == nullptr)
    {
        return;
    }

    conn->write_buf = data;
    conn->write_offset = 0;
    conn->state = Connection::Writing;


    if (!epoll_mod(cfd, EPOLLOUT))
    {
        close_connection(cfd);
        return;
    }

    handle_write(cfd);
}

void Server::handle_write(int cfd)
{
    Connection* conn = find_conn(cfd);
    if (conn == nullptr)
    {
        return;
    }

    while (conn->write_offset < conn->write_buf.size())
    {
        ssize_t w = write(cfd,
                          conn->write_buf.data() + conn->write_offset,
                          conn->write_buf.size() - conn->write_offset);

        if (w > 0)
        {

            conn->write_offset += static_cast<std::size_t>(w);
            continue;
        }

        if (w == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
        }

        close_connection(cfd);
        return;
    }

    close_connection(cfd);
}

void Server::sweep_idle()
{
    std::time_t now = time(nullptr);


    std::vector<int> stale;

    for (const auto& entry : conns_)
    {
        if (now - entry.second.last_active >= kIdleSeconds)
        {
            stale.push_back(entry.first);
        }
    }

    for (int cfd : stale)
    {
        close_connection(cfd);
    }
}
