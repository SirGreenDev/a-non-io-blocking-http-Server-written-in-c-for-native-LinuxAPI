#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "server.h"

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <port> <docroot>\n", argv[0]);
        fprintf(stderr, "Example: %s 8080 ./source\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);

    Server server(port, argv[2]);

    if (!server.init())
    {
        return 1;
    }

    server.run();

    return 0;
}
