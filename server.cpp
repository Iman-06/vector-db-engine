/*
server.cpp — TCP server entry point for the Vector DB Engine.
 CONCURRENCY MODEL:
   One thread per client.  Threads are detached (we never join them).
   The vector_store_t is shared across all threads;
   with a pthread_mutex inside vs_add() / vs_count() to protect against concurrent modifications.
 */

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
 
 //defaults used when the user omits an argument 
 constexpr int DEFAULT_PORT = 5556;
 constexpr int DEFAULT_DIM = 4;
 constexpr const char *DEFAULT_DATA = "./vdata";
 
//How many pending connections the kernel will queue before we accept() 
 constexpr int LISTEN_BACKLOG = 64;
 
 //client_arg_t — heap-allocated bundle passed to each client thread.
  // We heap-allocate it in the accept loop and the thread frees it.

 struct client_arg_t {
     int fd;                 // connected client socket
     server_config_t cfg;    // shallow copy of server config; cfg.store is shared across all threads 
 };
 
 //client_thread — entry point for each per-client pthread.
 // Receives a heap-allocated client_arg_t, calls handle_client() which runs until the client sends QUIT or drops, then closes the socket and frees the argument struct.

 static void *client_thread(void *arg) {
     auto *ca = static_cast<client_arg_t *>(arg);
 
     handle_client(ca->fd, &ca->cfg);
     close(ca->fd);
 
     delete ca;
     return nullptr;
 }
 
 //usage: print startup syntax and exit with failure.
 static void usage(const char *prog) {
     std::cerr
         << "Usage: " << prog << " --data <path> --dim <D> --port <P>\n"
         << "\n"
         << "  --data <path>   directory for persistent data (default: " << DEFAULT_DATA << ")\n"
         << "  --dim  <D>      fixed vector dimension, D > 0  (default: " << DEFAULT_DIM << ")\n"
         << "  --port <P>      TCP port to listen on, 1-65535 (default: " << DEFAULT_PORT << ")\n";
 
     std::exit(EXIT_FAILURE);
 }
 
 //parse_args: walk argv and populate cfg with --data / --dim / --port.
 static void parse_args(int argc, char **argv, server_config_t *cfg)//argc is the number of command-line arguments, argv is an array of strings (the arguments)
 {
     //set defaults
     cfg->port = DEFAULT_PORT;
     cfg->dim  = DEFAULT_DIM;
    cfg->data_path = DEFAULT_DATA;
 
     for (int i = 1; i < argc; i++) {
         std::string arg = argv[i];
 
         if (arg == "--data") {
             if (++i >= argc) {
                 std::cerr << "error: --data requires a path argument\n";
                 usage(argv[0]);
             }
 
             cfg->data_path = argv[i];
 
         } else if (arg == "--dim") {
             if (++i >= argc) {
                 std::cerr << "error: --dim requires an integer argument\n";
                 usage(argv[0]);
             }
 
             char *endp = nullptr;
             long d = std::strtol(argv[i], &endp, 10); //strtol converts a string to a long integer. It takes three arguments: the string to convert, a pointer to a char* that will be set to point to the first character after the number in the string, and the base for conversion (10 for decimal).
 
             if (*endp != '\0' || d <= 0) {
                 std::cerr << "error: --dim must be a positive integer\n";
                 usage(argv[0]);
             }
 
             cfg->dim = static_cast<int>(d);
 
         } else if (arg == "--port") {
             if (++i >= argc) {
                 std::cerr << "error: --port requires an integer argument\n";
                 usage(argv[0]);
             }
 
             char *endp = nullptr;
             long p = std::strtol(argv[i], &endp, 10);
 
             if (*endp != '\0' || p < 1 || p > 65535) {
                 std::cerr << "error: --port must be in range 1-65535\n";
                 usage(argv[0]);
             }
 
             cfg->port = static_cast<int>(p);
 
         } else {
             std::cerr << "error: unknown argument '" << argv[i] << "'\n";
             usage(argv[0]);
         }
     }
 }
 
 //parse_args -> vs_init -> socket -> bind -> listen -> accept loop
 int main(int argc, char **argv) {
     server_config_t cfg{}; // value-initializes cfg, ensuring that all fields are initialized to 0 or NULL
 
     //parse command-line arguments
     parse_args(argc, argv, &cfg);
 
     //initialise the vector store
     vector_store_t store{};
     int rc = vs_init(&store, cfg.dim);
 
     if (rc != VS_OK) {
         std::cerr << "fatal: vs_init failed (rc=" << rc << ")\n";
         return EXIT_FAILURE;
     }
 
     cfg.store = &store;   // share one store across all client threads 
 
     // create a TCP/IPv4 stream socket
     int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
     if (srv_fd < 0) {
         perror("socket");
         vs_destroy(&store);
         return EXIT_FAILURE;
     } //socket() creates a new socket and returns a file descriptor for it. AF_INET specifies that it's an IPv4 socket, SOCK_STREAM indicates that it's a TCP stream socket, and 0 means to use the default protocol. If the call fails, it returns -1
 
     // socket option reuse address
    // SO_REUSEADDR: lets us restart the server quickly after a crash without
    // waiting for the OS to release the port (avoids "Address already in use").
     
     int yes = 1;
     if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
         perror("setsockopt SO_REUSEADDR");   // non-fatal warning 
     }
 
     // bind to all interfaces on the chosen port
     sockaddr_in addr{}; //this struct stores ip address, port number and protocol family
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = INADDR_ANY;// accept connection from any ip
     addr.sin_port = htons(static_cast<std::uint16_t>(cfg.port)); //htons converts the port number from host byte order to network byte order (big-endian).
 
     if (bind(srv_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
         perror("bind");
         close(srv_fd);
         vs_destroy(&store);
         return EXIT_FAILURE;
     }
 
     // start listening
     if (listen(srv_fd, LISTEN_BACKLOG) < 0) {
         perror("listen");
         close(srv_fd);
         vs_destroy(&store);
         return EXIT_FAILURE;
     }
 
     std::cout
         << "[vdb] Server started\n"
         << "      port=" << cfg.port << "  dim=" << cfg.dim << "  data=" << cfg.data_path << "\n"
         << "      Waiting for clients…\n";
     std::cout.flush();
 
     // accept loop: for each accepted client, spawn a detached thread to handle it.
     for (;;) {
         sockaddr_in cli_addr{};// this struct will be filled in by accept() with the client's address information
         socklen_t cli_len = sizeof(cli_addr);
 
         int cli_fd = accept(srv_fd, reinterpret_cast<sockaddr *>(&cli_addr), &cli_len);
         if (cli_fd < 0) {
             // EINTR can happen if a signal interrupts accept.
             // It is non-fatal — just try again.
             if (errno == EINTR) continue;
             perror("accept");
             continue;
         }
 
         std::cout << "[vdb] Client connected  "
                   << inet_ntoa(cli_addr.sin_addr) // inet_ntoa converts the client's IP address to a human-readable string
                   << ":"
                   << ntohs(cli_addr.sin_port) // ntohs converts the client's port number from network byte order to host byte order
                   << "  (fd=" << cli_fd << ")\n";
         std::cout.flush();
 
         // build the per-thread argument: the thread frees it
         client_arg_t *ca = new (std::nothrow) client_arg_t;
         if (ca == nullptr) {
             std::cerr << "[vdb] OOM: dropping client fd=" << cli_fd << "\n";
             close(cli_fd);
             continue;
         }
 
         ca->fd = cli_fd;
        ca->cfg = cfg;   // shallow copy; ca->cfg.store == &store (shared) 
 
         // spawn the thread: it will free ca when done
         pthread_t tid;
         pthread_attr_t attr;
         pthread_attr_init(&attr);
         pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 
         if (pthread_create(&tid, &attr, client_thread, ca) != 0) {
             perror("pthread_create");
             delete ca;
             close(cli_fd);
         }
 
         pthread_attr_destroy(&attr);
     }
 
     close(srv_fd);
     vs_destroy(&store);
     return 0;
 }
