#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> // for getaddrinfo for dns
#define MAX_LINE 4096
using namespace std;
static int tcp_connect(const string& host, const string& port)
{
    addrinfo hints{}; // tells type of socket we want
    addrinfo* result; //stores list of possible addresses returned by getaddrinfo
    hints.ai_family = AF_UNSPEC; //ipv4 or ipv6
    hints.ai_socktype = SOCK_STREAM; //tcp
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) { 
        cerr << "DNS resolution failed\n";
        return -1;
    }
    int sockfd = -1;
    for (addrinfo* rp = result; rp; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0)
            continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(result); //free the linked list allocated by getaddrinfo
    return sockfd;
}
static void read_response(FILE* in)
{
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        cout << line;
        if (line[0] == '(') break;
        if (strncmp(line, "OK", 2) == 0) break; 
        if (strncmp(line, "ERROR", 5) == 0) break;
    }
}
int main(int argc, char* argv[])
{
    if (argc != 3) {
        cerr << "Usage: ./client <host> <port>\n";
        return 1;
    }
    string host = argv[1];
    string port = argv[2];
    int sock = tcp_connect(host, port);
    if (sock < 0) {
        cerr << "Connection failed\n";
        return 1;
    }
    cout << "Connected to vdb at " << host << ":" << port << "\n";
    FILE* in  = fdopen(dup(sock), "r"); // for fgets and fprintf to work without messing with the socket's file position
    FILE* out = fdopen(sock, "w");
    if (!in || !out) {
        perror("fdopen");
        return 1;
    }
    setbuf(out, nullptr); //this disables buffering so that cmnds r sent instantly
    string input;
    while (true) {
        cout << "> ";
        getline(cin, input);
        if (!cin)
            break;
        if (input.empty())
            continue;
        fprintf(out, "%s\n", input.c_str());
        fflush(out);
        if (input == "QUIT") {
            char tmp[MAX_LINE];
            if (fgets(tmp, sizeof(tmp), in))
                cout << tmp; //final response from server before closing
            cout << "(disconnected)\n";
            break;
        }
        read_response(in); 
    }
    fclose(in);
    fclose(out);
    return 0;
}