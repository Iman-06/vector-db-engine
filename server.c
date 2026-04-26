/*
 * server.c — TCP server entry point for the Vector DB Engine.
 * Member 1 owns this file.
 *
 * START THE SERVER:
 *   ./vdb --data ./vdata --dim 4 --port 5556
 *
 * WHAT THIS FILE DOES:
 *   1. parse_args()  — read --data / --dim / --port from argv.
 *   2. vs_init()     — initialise the vector store (Member 2).
 *   3. Create a TCP socket, bind it to the chosen port, start listening.
 *   4. Accept loop   — for every incoming client, allocate a client_arg_t
 *                      on the heap and spawn a detached pthread that runs
 *                      client_thread().
 *   5. client_thread() calls handle_client() (command.c) which reads and
 *      dispatches commands until the client disconnects or sends QUIT.
 *
 * CONCURRENCY MODEL:
 *   One thread per client.  Threads are detached (we never join them).
 *   The vector_store_t is shared across all threads; Member 2 protects it
 *   with a pthread_mutex inside vs_add() / vs_count(), and Member 1 holds
 *   the lock explicitly for the duration of SEARCH (see command.c).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "vdb_interface.h"   /* server_config_t, search_result_t, … */
#include "command.h"          /* handle_client() */

/* ── defaults used when the user omits an argument ── */
#define DEFAULT_PORT   5556
#define DEFAULT_DIM    4
#define DEFAULT_DATA   "./vdata"

/* How many pending connections the kernel will queue before we accept() */
#define LISTEN_BACKLOG 64

/* ──────────────────────────────────────────────────────────────────────
 * client_arg_t — heap-allocated bundle passed to each client thread.
 *
 * We heap-allocate it in the accept loop and the thread frees it.
 * This avoids the classic bug of passing a stack pointer to a thread
 * that outlives the stack frame.
 * ────────────────────────────────────────────────────────────────────── */
typedef struct {
    int             fd;    /* connected client socket               */
    server_config_t cfg;   /* shallow copy of server config;
                              cfg.store is a pointer — intentionally
                              shared across all threads             */
} client_arg_t;

/* ──────────────────────────────────────────────────────────────────────
 * client_thread — entry point for each per-client pthread.
 *
 * Receives a heap-allocated client_arg_t, calls handle_client() which
 * runs until the client sends QUIT or drops, then closes the socket and
 * frees the argument struct.
 * ────────────────────────────────────────────────────────────────────── */
static void *client_thread(void *arg) {
    client_arg_t *ca = (client_arg_t *)arg;

    handle_client(ca->fd, &ca->cfg);

    close(ca->fd);
    free(ca);
    return NULL;
}

/* ──────────────────────────────────────────────────────────────────────
 * usage — print startup syntax and exit with failure.
 * ────────────────────────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s --data <path> --dim <D> --port <P>\n"
            "\n"
            "  --data <path>   directory for persistent data (default: %s)\n"
            "  --dim  <D>      fixed vector dimension, D > 0  (default: %d)\n"
            "  --port <P>      TCP port to listen on, 1-65535 (default: %d)\n",
            prog, DEFAULT_DATA, DEFAULT_DIM, DEFAULT_PORT);
    exit(EXIT_FAILURE);
}

/* ──────────────────────────────────────────────────────────────────────
 * parse_args — walk argv and populate cfg with --data / --dim / --port.
 *
 * Fills in defaults first, then overwrites with any supplied values.
 * Calls usage() (and exits) on bad or missing argument values.
 * ────────────────────────────────────────────────────────────────────── */
static void parse_args(int argc, char **argv, server_config_t *cfg) {
    /* set defaults */
    cfg->port = DEFAULT_PORT;
    cfg->dim  = DEFAULT_DIM;
    strncpy(cfg->data_path, DEFAULT_DATA, sizeof(cfg->data_path) - 1);
    cfg->data_path[sizeof(cfg->data_path) - 1] = '\0';
    cfg->store = NULL;   /* filled in after vs_init() */

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "--data") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --data requires a path argument\n");
                usage(argv[0]);
            }
            strncpy(cfg->data_path, argv[i], sizeof(cfg->data_path) - 1);
            cfg->data_path[sizeof(cfg->data_path) - 1] = '\0';

        } else if (strcmp(argv[i], "--dim") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --dim requires an integer argument\n");
                usage(argv[0]);
            }
            char *endp;
            long d = strtol(argv[i], &endp, 10);
            if (*endp != '\0' || d <= 0) {
                fprintf(stderr, "error: --dim must be a positive integer\n");
                usage(argv[0]);
            }
            cfg->dim = (int)d;

        } else if (strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --port requires an integer argument\n");
                usage(argv[0]);
            }
            char *endp;
            long p = strtol(argv[i], &endp, 10);
            if (*endp != '\0' || p < 1 || p > 65535) {
                fprintf(stderr, "error: --port must be in range 1-65535\n");
                usage(argv[0]);
            }
            cfg->port = (int)p;

        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            usage(argv[0]);
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────
 * main — program entry point.
 *
 * Order of operations:
 *   parse_args → vs_init → socket → bind → listen → accept loop
 * ────────────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* 1. parse command-line arguments */
    parse_args(argc, argv, &cfg);

    /* 2. initialise the vector store (Member 2's function) */
    vector_store_t store;
    int rc = vs_init(&store, cfg.dim);
    if (rc != VS_OK) {
        fprintf(stderr, "fatal: vs_init failed (rc=%d)\n", rc);
        return EXIT_FAILURE;
    }
    cfg.store = &store;   /* share one store across all client threads */

    /* 3. create a TCP/IPv4 stream socket */
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        perror("socket");
        vs_destroy(&store);
        return EXIT_FAILURE;
    }

    /*
     * SO_REUSEADDR: lets us restart the server quickly after a crash without
     * waiting for the OS to release the port (avoids "Address already in use").
     */
    int yes = 1;
    if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        perror("setsockopt SO_REUSEADDR");   /* non-fatal warning */

    /* 4. bind to all interfaces on the chosen port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)cfg.port);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv_fd);
        vs_destroy(&store);
        return EXIT_FAILURE;
    }

    /* 5. start listening */
    if (listen(srv_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(srv_fd);
        vs_destroy(&store);
        return EXIT_FAILURE;
    }

    fprintf(stdout,
            "[vdb] Server started\n"
            "      port=%d  dim=%d  data=%s\n"
            "      Waiting for clients…\n",
            cfg.port, cfg.dim, cfg.data_path);
    fflush(stdout);

    /* ── 6. accept loop ──────────────────────────────────────────────
     * For each new connection:
     *   a. accept()  — blocks until a client connects
     *   b. allocate client_arg_t on the heap
     *   c. pthread_create() with PTHREAD_CREATE_DETACHED so the thread
     *      cleans itself up when it finishes (no pthread_join needed)
     * ─────────────────────────────────────────────────────────────── */
    for (;;) {
        struct sockaddr_in cli_addr;
        socklen_t          cli_len = sizeof(cli_addr);

        int cli_fd = accept(srv_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) {
            /*
             * EINTR can happen if a signal (e.g., SIGCHLD) interrupts accept.
             * It is non-fatal — just try again.
             */
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        fprintf(stdout, "[vdb] Client connected  %s:%d  (fd=%d)\n",
                inet_ntoa(cli_addr.sin_addr),
                ntohs(cli_addr.sin_port),
                cli_fd);
        fflush(stdout);

        /* build the per-thread argument — the thread frees it */
        client_arg_t *ca = malloc(sizeof(client_arg_t));
        if (!ca) {
            fprintf(stderr, "[vdb] OOM: dropping client fd=%d\n", cli_fd);
            close(cli_fd);
            continue;
        }
        ca->fd  = cli_fd;
        ca->cfg = cfg;   /* shallow copy; ca->cfg.store == &store (shared) */

        pthread_t      tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, client_thread, ca) != 0) {
            perror("pthread_create");
            free(ca);
            close(cli_fd);
        }
        pthread_attr_destroy(&attr);
    }

    /* Unreachable in normal operation — no graceful shutdown in Phase 1 */
    close(srv_fd);
    vs_destroy(&store);
    return 0;
}
