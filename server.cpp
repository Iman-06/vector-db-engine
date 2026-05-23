#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "vdb_interface.h"
#include "command.h"

constexpr int DEFAULT_PORT = 5556;
constexpr int DEFAULT_DIM  = 4;
constexpr const char* DEFAULT_DATA = "./vdata";
constexpr int LISTEN_BACKLOG = 64;

struct client_arg_t {
    int fd;
    server_config_t cfg;
};

static void* client_thread(void* arg) {
    auto* ca = static_cast<client_arg_t*>(arg);
    handle_client(ca->fd, &ca->cfg);
    close(ca->fd);
    delete ca;
    return nullptr;
}

static void usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " --data <path> --dim <D> --port <P> [--metric euclidean|cosine]\n"
        << "\n"
        << "  --data   <path>             data directory (default: " << DEFAULT_DATA << ")\n"
        << "  --dim    <D>                vector dimension (default: " << DEFAULT_DIM << ")\n"
        << "  --port   <P>                TCP port (default: " << DEFAULT_PORT << ")\n"
        << "  --metric euclidean|cosine   distance metric (default: euclidean)\n";
    std::exit(EXIT_FAILURE);
}

static void parse_args(int argc, char** argv, server_config_t* cfg) {
    cfg->port      = DEFAULT_PORT;
    cfg->dim       = DEFAULT_DIM;
    cfg->data_path = DEFAULT_DATA;
    cfg->metric    = MetricType::EUCLIDEAN;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--data") {
            if (++i >= argc) { std::cerr << "error: --data requires a path\n"; usage(argv[0]); }
            cfg->data_path = argv[i];

        } else if (arg == "--dim") {
            if (++i >= argc) { std::cerr << "error: --dim requires an integer\n"; usage(argv[0]); }
            char* endp;
            long d = std::strtol(argv[i], &endp, 10);
            if (*endp != '\0' || d <= 0) { std::cerr << "error: --dim must be positive\n"; usage(argv[0]); }
            cfg->dim = (int)d;

        } else if (arg == "--port") {
            if (++i >= argc) { std::cerr << "error: --port requires an integer\n"; usage(argv[0]); }
            char* endp;
            long p = std::strtol(argv[i], &endp, 10);
            if (*endp != '\0' || p < 1 || p > 65535) { std::cerr << "error: --port must be 1-65535\n"; usage(argv[0]); }
            cfg->port = (int)p;

        } else if (arg == "--metric") {
            if (++i >= argc) { std::cerr << "error: --metric requires euclidean or cosine\n"; usage(argv[0]); }
            std::string m = argv[i];
            if (m == "cosine")     cfg->metric = MetricType::COSINE;
            else if (m == "euclidean") cfg->metric = MetricType::EUCLIDEAN;
            else { std::cerr << "error: unknown metric '" << m << "'\n"; usage(argv[0]); }

        } else {
            std::cerr << "error: unknown argument '" << arg << "'\n";
            usage(argv[0]);
        }
    }
}

int main(int argc, char** argv) {
    server_config_t cfg{};
    parse_args(argc, argv, &cfg);

    vector_store_t store{};
    if (vs_init(&store, cfg.dim) != VS_OK) {
        std::cerr << "fatal: vs_init failed\n";
        return EXIT_FAILURE;
    }
    cfg.store = &store;

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("socket"); vs_destroy(&store); return EXIT_FAILURE; }

    int yes = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)cfg.port);

    if (bind(srv_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv_fd); vs_destroy(&store); return EXIT_FAILURE;
    }
    if (listen(srv_fd, LISTEN_BACKLOG) < 0) {
        perror("listen"); close(srv_fd); vs_destroy(&store); return EXIT_FAILURE;
    }

    std::cout
        << "[vdb] Server started\n"
        << "      port="   << cfg.port
        << "  dim="        << cfg.dim
        << "  data="       << cfg.data_path
        << "  metric="     << (cfg.metric == MetricType::COSINE ? "cosine" : "euclidean")
        << "\n      Waiting for clients...\n";
    std::cout.flush();

    for (;;) {
        sockaddr_in cli{};
        socklen_t cli_len = sizeof(cli);
        int cli_fd = accept(srv_fd, (sockaddr*)&cli, &cli_len);
        if (cli_fd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        std::cout << "[vdb] Client connected " << inet_ntoa(cli.sin_addr)
                  << ":" << ntohs(cli.sin_port) << "\n";
        std::cout.flush();

        client_arg_t* ca = new (std::nothrow) client_arg_t;
        if (!ca) { std::cerr << "[vdb] OOM: dropping client\n"; close(cli_fd); continue; }
        ca->fd  = cli_fd;
        ca->cfg = cfg;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, client_thread, ca) != 0) {
            perror("pthread_create"); delete ca; close(cli_fd);
        }
        pthread_attr_destroy(&attr);
    }

    close(srv_fd);
    vs_destroy(&store);
    return 0;
}