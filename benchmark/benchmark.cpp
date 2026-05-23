// benchmark/benchmark.cpp
// Benchmark client for the Vector DB Engine.
// Connects to the running server via TCP, inserts 50,000 64-D vectors,
// builds the IVF index, then measures query performance for BRUTE and
// IVF at nprobe = 1, 5, 10, 25.  Prints a results table and writes it
// to benchmark/results.txt.

#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <random>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

// Benchmark parameters 
static constexpr int DIM       = 64;
static constexpr int N_VECTORS = 50000;
static constexpr int N_QUERIES = 100;
static constexpr int K         = 10;
static constexpr int SEED      = 42;

// TCP helpers 

static int tcp_connect(const std::string& host, const std::string& port)
{
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res     = nullptr;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        std::cerr << "getaddrinfo failed for " << host << ':' << port << '\n';
        return -1;
    }

    int fd = -1;
    for (addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void set_recv_timeout(int fd, int seconds)
{
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

//  Vector generation

static std::vector<float> random_vector(int dim, std::mt19937& rng)
{
    std::normal_distribution<float> nd(0.0f, 1.0f);
    std::vector<float> v(static_cast<size_t>(dim));
    for (int i = 0; i < dim; i++) v[i] = nd(rng);
    return v;
}

// Command builders 

static std::string make_add_command(uint64_t id, const std::vector<float>& v)
{
    std::ostringstream oss;
    oss << "ADD " << id << std::fixed << std::setprecision(8);
    for (float f : v) oss << ' ' << f;
    oss << '\n';
    return oss.str();
}

static std::string make_search_command(const std::vector<float>& q, int k,
                                       const std::string& mode, int nprobe = 0)
{
    std::ostringstream oss;
    oss << "SEARCH" << std::fixed << std::setprecision(8);
    for (float f : q) oss << ' ' << f;
    oss << ' ' << k << ' ' << mode;
    if (mode == "IVF") oss << ' ' << nprobe;
    oss << '\n';
    return oss.str();
}

//  Response I/O 

// Read lines until a terminal line: starts with '(', "OK", "ERR", or "BYE".
static std::string read_response(FILE* in)
{
    char buf[16384];
    std::string resp;
    while (fgets(buf, sizeof(buf), in)) {
        resp += buf;
        char c = buf[0];
        if (c == '(')                    break;   // summary / end-of-results
        if (strncmp(buf, "OK",  2) == 0) break;
        if (strncmp(buf, "ERR", 3) == 0) break;
        if (strncmp(buf, "BYE", 3) == 0) break;
    }
    return resp;
}

// Send cmd to the server and return the full response string.
static std::string send_command(FILE* in, FILE* out, const std::string& cmd)
{
    if (fputs(cmd.c_str(), out) == EOF || fflush(out) != 0) {
        std::cerr << "send_command: write error\n";
        return "";
    }
    return read_response(in);
}

//  Response parsers 

// Extract result IDs from a SEARCH response.
// Each result line format: "<id> <distance> [v1 v2 ...]"
// The summary line starts with '(' and is skipped.
static std::vector<uint64_t> parse_result_ids(const std::string& resp)
{
    std::vector<uint64_t> ids;
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '(') continue;
        if (strncmp(line.c_str(), "ERR", 3) == 0) break;
        std::istringstream ls(line);
        uint64_t id;
        if (ls >> id) ids.push_back(id);
    }
    return ids;
}

// Extract the scanned count from the summary line.
// Pattern: "... scanned N)" — atoi stops at ')'.
static int parse_scanned_count(const std::string& resp)
{
    auto pos = resp.find("scanned ");
    if (pos == std::string::npos) return 0;
    return std::atoi(resp.c_str() + pos + 8);   // 8 == len("scanned ")
}

// Extract cluster count from BUILD response.
// Pattern line: "clusters  : K"
static int parse_cluster_count(const std::string& resp)
{
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("clusters", 0) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos)
                return std::atoi(line.c_str() + colon + 1);
        }
    }
    return 0;
}

//  Recall@10 

// recall@10 = |brute_top10 ∩ ivf_top10| / 10
static double recall_at_10(const std::vector<uint64_t>& brute,
                            const std::vector<uint64_t>& ivf)
{
    std::unordered_set<uint64_t> gt(brute.begin(), brute.end());
    int common = 0;
    for (uint64_t id : ivf)
        if (gt.count(id)) ++common;
    return static_cast<double>(common) / K;
}

//  Timing 

using Clock = std::chrono::high_resolution_clock;
using TP    = std::chrono::time_point<Clock>;

static double ms_between(TP a, TP b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

//  Main 

int main(int argc, char* argv[])
{
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> [output_file]\n";
        return 1;
    }

    // Optional 3rd argument: output file path (default: benchmark/results.txt)
    std::string output_path = (argc == 4) ? argv[3] : "benchmark/results.txt";

    //  Connect 
    int fd = tcp_connect(argv[1], argv[2]);
    if (fd < 0) {
        std::cerr << "Could not connect to " << argv[1] << ':' << argv[2] << '\n';
        return 1;
    }
    std::cout << "Connected to " << argv[1] << ':' << argv[2] << '\n';

    set_recv_timeout(fd, 60);          // 60 s per recv — enough for each ADD "OK"

    FILE* in  = fdopen(dup(fd), "r");
    FILE* out = fdopen(fd,      "w");
    if (!in || !out) { perror("fdopen"); return 1; }
    setbuf(out, nullptr);              // unbuffered writes → immediate send

    //  Phase 1: Generate vectors 
    std::cout << "Generating " << N_VECTORS << " x " << DIM
              << "D vectors (seed=" << SEED << ", normal(0,1))...\n";

    std::mt19937 rng(SEED);
    std::vector<std::vector<float>> vectors(static_cast<size_t>(N_VECTORS));
    for (int i = 0; i < N_VECTORS; i++)
        vectors[i] = random_vector(DIM, rng);

        // ── Phase 2: Insert all vectors 
    std::cout << "Inserting " << N_VECTORS << " vectors...\n";
    auto t_ins0 = Clock::now();

    for (int i = 0; i < N_VECTORS; i++) {
        std::string resp = send_command(in, out,
                               make_add_command(static_cast<uint64_t>(i), vectors[i]));
        if (resp.rfind("OK", 0) != 0) {
            std::cerr << "ADD " << i << " failed: " << resp;
            fclose(in); fclose(out);
            return 1;
        }
        if ((i + 1) % 10000 == 0)
            std::cout << "  " << (i + 1) << " / " << N_VECTORS << '\n';
    }

    auto   t_ins1    = Clock::now();
    double insert_ms = ms_between(t_ins0, t_ins1);
    double inserts_s = N_VECTORS / (insert_ms / 1000.0);

    std::cout << "Insert done: " << std::fixed << std::setprecision(1)
              << insert_ms << " ms  ("
              << static_cast<int>(inserts_s) << " inserts/sec)\n";

    //  Phase 3: BUILD IVF index 
    // k-means over 50 K vectors can take up to a few minutes; extend timeout.
    set_recv_timeout(fd, 300);

    std::cout << "Building IVF index...\n";
    auto t_build0  = Clock::now();
    std::string br = send_command(in, out, "BUILD\n");
    auto t_build1  = Clock::now();
    double build_ms = ms_between(t_build0, t_build1);

    if (br.find("OK") == std::string::npos) {
        std::cerr << "BUILD failed:\n" << br;
        fclose(in); fclose(out);
        return 1;
    }

    int clusters = parse_cluster_count(br);
    std::cout << "Build done: " << std::fixed << std::setprecision(1)
              << build_ms << " ms  (" << clusters << " clusters)\n";

    //  Phase 4: Query vectors 
    set_recv_timeout(fd, 30);

    std::cout << "Generating " << N_QUERIES << " query vectors...\n";
    std::vector<std::vector<float>> queries(static_cast<size_t>(N_QUERIES));
    for (int i = 0; i < N_QUERIES; i++)
        queries[i] = random_vector(DIM, rng);    // continues from same rng state

    struct ModeResult {
        std::string         label;
        int                 nprobe;
        std::vector<double> times_ms;
        std::vector<double> recalls;
        std::vector<int>    scanned;
    };

    std::vector<ModeResult> modes = {
        {"BRUTE",  0, {}, {}, {}},
        {"IVF",    1, {}, {}, {}},
        {"IVF",    5, {}, {}, {}},
        {"IVF",   10, {}, {}, {}},
        {"IVF",   25, {}, {}, {}},
    };

    std::cout << "Running " << N_QUERIES
              << " queries (BRUTE + IVF nprobe=1,5,10,25)...\n";

    for (int qi = 0; qi < N_QUERIES; qi++) {
        const auto& q = queries[qi];

        // BRUTE — ground truth
        auto ta0 = Clock::now();
        auto br0 = send_command(in, out, make_search_command(q, K, "BRUTE"));
        auto tb0 = Clock::now();

        std::vector<uint64_t> brute_ids = parse_result_ids(br0);
        modes[0].times_ms.push_back(ms_between(ta0, tb0));
        modes[0].recalls.push_back(1.0);
        modes[0].scanned.push_back(parse_scanned_count(br0));

        // IVF with each nprobe value
        const int nprobes[] = {1, 5, 10, 25};
        for (int ni = 0; ni < 4; ni++) {
            auto ta = Clock::now();
            auto ir = send_command(in, out,
                           make_search_command(q, K, "IVF", nprobes[ni]));
            auto tb = Clock::now();

            std::vector<uint64_t> ivf_ids = parse_result_ids(ir);
            modes[ni + 1].times_ms.push_back(ms_between(ta, tb));
            modes[ni + 1].recalls.push_back(recall_at_10(brute_ids, ivf_ids));
            modes[ni + 1].scanned.push_back(parse_scanned_count(ir));
        }

        if ((qi + 1) % 25 == 0)
            std::cout << "  query " << (qi + 1) << " / " << N_QUERIES << '\n';
    }

    // Disconnect cleanly
    send_command(in, out, "QUIT\n");
    fclose(in);
    fclose(out);

    //  Build results table 
    auto avg_d = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    auto avg_i = [](const std::vector<int>& v) -> double {
        if (v.empty()) return 0.0;
        double s = 0;
        for (int x : v) s += x;
        return s / v.size();
    };

    // Column widths matching the header spec:
    //   Mode | nprobe | Avg Query Time (ms) | Recall@10 | Avg Scanned
    const int W_MODE    =  8;
    const int W_NPROBE  =  6;
    const int W_TIME    = 19;
    const int W_RECALL  =  9;
    const int W_SCANNED = 11;

    std::ostringstream tbl;
    tbl << '\n'
        << "================================================================\n"
        << "             VECTOR DATABASE BENCHMARK RESULTS\n"
        << "================================================================\n"
        << "Vectors    : " << N_VECTORS << '\n'
        << "Dimensions : " << DIM       << '\n'
        << "Queries    : " << N_QUERIES << '\n'
        << "k          : " << K         << '\n'
        << "Seed       : " << SEED      << '\n'
        << '\n'
        << "Insert Performance:\n"
        << "  Total time : " << std::fixed << std::setprecision(1)
                             << insert_ms << " ms\n"
        << "  Throughput : " << static_cast<int>(inserts_s) << " inserts/sec\n"
        << '\n'
        << "IVF Index Build:\n"
        << "  Build time : " << std::fixed << std::setprecision(1)
                             << build_ms << " ms\n"
        << "  Clusters   : " << clusters << '\n'
        << '\n'
        << "Query Performance:\n";

    // Header row
    tbl << std::left  << std::setw(W_MODE)    << "Mode"
        << " | "
        << std::right << std::setw(W_NPROBE)  << "nprobe"
        << " | "
        << std::right << std::setw(W_TIME)    << "Avg Query Time (ms)"
        << " | "
        << std::right << std::setw(W_RECALL)  << "Recall@10"
        << " | "
        << std::right << std::setw(W_SCANNED) << "Avg Scanned"
        << '\n';

    // Separator
    tbl << std::string(W_MODE,    '-') << "-+-"
        << std::string(W_NPROBE,  '-') << "-+-"
        << std::string(W_TIME,    '-') << "-+-"
        << std::string(W_RECALL,  '-') << "-+-"
        << std::string(W_SCANNED, '-') << '\n';

    // Data rows
    for (auto& m : modes) {
        std::string np_str = (m.nprobe > 0) ? std::to_string(m.nprobe) : "-";
        tbl << std::left  << std::setw(W_MODE)    << m.label
            << " | "
            << std::right << std::setw(W_NPROBE)  << np_str
            << " | "
            << std::right << std::fixed << std::setprecision(3)
                          << std::setw(W_TIME)    << avg_d(m.times_ms)
            << " | "
            << std::right << std::fixed << std::setprecision(4)
                          << std::setw(W_RECALL)  << avg_d(m.recalls)
            << " | "
            << std::right << std::setw(W_SCANNED) << static_cast<int>(avg_i(m.scanned))
            << '\n';
    }
    tbl << "================================================================\n";

    std::string table_str = tbl.str();
    std::cout << table_str;

    // Save to the requested output file
    std::ofstream outfile(output_path);
    if (outfile) {
        outfile << table_str;
        std::cout << "\nSaved to " << output_path << '\n';
    } else {
        std::cerr << "Warning: could not open " << output_path << " for writing\n";
    }

    return 0;
}
