// command.cpp: Command parsing, dispatch, and response formatting.
#define _POSIX_C_SOURCE 200809L

#include "command.h"
#include "vdb_interface.h"
#include "vector_store.h"
#include "ivf.h"
#include "normalize.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include <ext/stdio_filebuf.h>

using namespace std;

ivf_index_t g_ivf_index;

#define MAX_LINE  8192
#define MAX_K     10000

static void send_fmt(int fd, const char *fmt, ...) {
    char buf[MAX_LINE + 4];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (buf[n - 1] != '\n') { buf[n] = '\n'; n++; }
    write(fd, buf, (size_t)n);
}

// cmd_add: handle ADD <id> <v1> <v2> ... <vD>
static void cmd_add(int fd, const server_config_t *cfg, string rest) {
    const int dim = cfg->dim;
    string tok;
    char *endp;
    stringstream ss(rest);

    if (!(ss >> tok)) { send_fmt(fd, "ERR ADD: missing id"); return; }
    int64_t id = strtoll(tok.c_str(), &endp, 10);
    if (*endp != '\0') { send_fmt(fd, "ERR ADD: bad id '%s'", tok.c_str()); return; }

    vector<float> vec((size_t)dim);
    for (int i = 0; i < dim; i++) {
        if (!(ss >> tok)) { send_fmt(fd, "ERR ADD: expected %d components, got %d", dim, i); return; }
        vec[i] = strtof(tok.c_str(), &endp);
        if (*endp != '\0') { send_fmt(fd, "ERR ADD: bad float '%s' at component %d", tok.c_str(), i); return; }
    }
    if (ss >> tok) { send_fmt(fd, "ERR ADD: too many values (server dim = %d)", dim); return; }
    if (cfg->metric == MetricType::COSINE) {
        int nrc = normalize_vec(vec);
        if (nrc == NORM_ERR_ZERO) {
            send_fmt(fd, "ERR ADD: zero vector cannot be normalized for cosine metric");
            return;
        }
    }

    int rc = vs_add(cfg->store, id, vec.data());
    if (rc == VS_OK && g_ivf_index.built) {
        vs_lock(cfg->store);
        size_t new_idx = cfg->store->count - 1;
        const float* vec_ptr = vs_get_vector(cfg->store, new_idx);
        ivf_insert(g_ivf_index, vec_ptr, (int)new_idx);
        vs_unlock(cfg->store);
    }
    switch (rc) {
        case VS_OK:        send_fmt(fd, "OK"); break;
        case VS_ERR_NOMEM: send_fmt(fd, "ERR ADD: out of memory"); break;
        default:           send_fmt(fd, "ERR ADD: store error %d", rc); break;
    }
}

// cmd_search: handle SEARCH <v1> ... <vD> <k> BRUTE|IVF [nprobe]
static void cmd_search(int fd, const server_config_t* cfg, string rest) {
    const int dim = cfg->dim;
    string tok;
    char* endp;
    stringstream ss(rest);

    // parse query vector
    vector<float> query((size_t)dim);
    for (int i = 0; i < dim; i++) {
        if (!(ss >> tok)) { send_fmt(fd, "ERR SEARCH: expected %d components, got %d", dim, i); return; }
        query[i] = strtof(tok.c_str(), &endp);
        if (*endp != '\0') { send_fmt(fd, "ERR SEARCH: bad float '%s' at component %d", tok.c_str(), i); return; }
    }
    if (cfg->metric == MetricType::COSINE) {
        int nrc = normalize_vec(query);
        if (nrc == NORM_ERR_ZERO) {
            send_fmt(fd, "ERR SEARCH: zero query vector cannot be used with cosine metric");
            return;
        }
    }

    // parse k
    if (!(ss >> tok)) { send_fmt(fd, "ERR SEARCH: missing k"); return; }
    long k = strtol(tok.c_str(), &endp, 10);
    if (*endp != '\0' || k <= 0 || k > MAX_K) {
        send_fmt(fd, "ERR SEARCH: bad k '%s' (must be 1..%d)", tok.c_str(), MAX_K);
        return;
    }

    // parse mode
    if (!(ss >> tok)) { send_fmt(fd, "ERR SEARCH: missing mode (expected BRUTE or IVF)"); return; }

    search_mode_t mode;
    int nprobe = 1;

    if (tok == "BRUTE") {
        mode = search_mode_t::SEARCH_MODE_BRUTE;
    } else if (tok == "IVF") {
        mode = search_mode_t::SEARCH_MODE_ANN;
        if (!(ss >> tok)) { send_fmt(fd, "ERR SEARCH: IVF mode requires nprobe parameter"); return; }
        nprobe = (int)strtol(tok.c_str(), &endp, 10);
        if (*endp != '\0' || nprobe <= 0) { send_fmt(fd, "ERR SEARCH: bad nprobe '%s'", tok.c_str()); return; }
    } else {
        send_fmt(fd, "ERR SEARCH: unknown mode '%s' (expected BRUTE or IVF)", tok.c_str());
        return;
    }

    vector<search_result_t> results((size_t)k);
    vs_lock(cfg->store);

    size_t scanned = 0;
    int out_count = 0;
    int rc;

    if (mode == search_mode_t::SEARCH_MODE_BRUTE) {
        rc = search_brute(*cfg->store, query, (int)k, results, out_count, cfg->metric);
        scanned = cfg->store->count;
    } else {
        if (!g_ivf_index.built) {
            vs_unlock(cfg->store);
            send_fmt(fd, "ERR SEARCH: IVF index not built. Use BUILD first.");
            return;
        }
        rc = search_ivf(*cfg->store, g_ivf_index, query, (int)k, nprobe,
                        results, out_count, scanned, cfg->metric);
    }

    // snapshot vectors while still locked
    vector<float> snap;
    if (rc == VS_OK && out_count > 0) {
        snap.resize((size_t)out_count * (size_t)dim);
        for (int i = 0; i < out_count; i++) {
            const float* v = vs_get_vector(cfg->store, results[i].store_index);
            if (v) memcpy(snap.data() + (size_t)i * (size_t)dim, v, (size_t)dim * sizeof(float));
        }
    }
    vs_unlock(cfg->store);

    if (rc != VS_OK) {
        send_fmt(fd, "ERR SEARCH: search failed (rc=%d)", rc);
        return;
    }

    const char* metric_name = (cfg->metric == MetricType::COSINE) ? "cosine" : "euclidean";

    for (int i = 0; i < out_count; i++) {
        ostringstream line;
        line.setf(ios::fixed);
        line.precision(6);
        line << (long long)results[i].id << " " << results[i].distance;
        if (!snap.empty()) {
            const float* v = snap.data() + (size_t)i * (size_t)dim;
            for (int j = 0; j < dim; j++) {
                if ((int)line.str().size() >= MAX_LINE - 16) break;
                line << " " << v[j];
            }
        }
        string out = line.str() + "\n";
        write(fd, out.c_str(), out.size());
    }

    if (mode == search_mode_t::SEARCH_MODE_BRUTE)
        send_fmt(fd, "(%d results, mode=BRUTE, metric=%s, scanned %zu)", out_count, metric_name, scanned);
    else
        send_fmt(fd, "(%d results, mode=IVF, nprobe=%d, metric=%s, scanned %zu)", out_count, nprobe, metric_name, scanned);
}

// cmd_stats: handle STATS
static void cmd_stats(int fd, const server_config_t *cfg) {
    vs_lock(cfg->store);
    int    dim   = cfg->store->dim;
    size_t count = cfg->store->count;
    vs_unlock(cfg->store);

    send_fmt(fd, "dimension     : %d",  dim);
    send_fmt(fd, "total vectors : %zu", count);
    send_fmt(fd, "metric        : %s",  cfg->metric == MetricType::COSINE ? "cosine" : "euclidean");

    if (g_ivf_index.built) {
        send_fmt(fd, "index built   : yes");
        send_fmt(fd, "clusters      : %d", g_ivf_index.k);
        string sizes;
        for (int c = 0; c < g_ivf_index.k; c++) {
            if (c > 0) sizes += ", ";
            sizes += to_string(g_ivf_index.clusters[(size_t)c].size());
        }
        send_fmt(fd, "cluster sizes : %s", sizes.c_str());
    } else {
        send_fmt(fd, "index built   : no");
        send_fmt(fd, "clusters      : N/A");
    }
}

// cmd_build: handle BUILD
static void cmd_build(int fd, const server_config_t *cfg) {
    vs_lock(cfg->store);
    if (cfg->store->count == 0) {
        vs_unlock(cfg->store);
        send_fmt(fd, "ERR BUILD: no vectors stored");
        return;
    }
    send_fmt(fd, "Building IVF index ...");
    int rc = ivf_build(*cfg->store, g_ivf_index);
    vs_unlock(cfg->store);

    if (rc != VS_OK) { send_fmt(fd, "ERR BUILD: k-means failed (rc=%d)", rc); return; }
    send_fmt(fd, "vectors   : %zu",  cfg->store->count);
    send_fmt(fd, "clusters  : %d",   g_ivf_index.k);
    send_fmt(fd, "iterations: %d",   g_ivf_index.iterations);
    send_fmt(fd, "done in %.3f s.",  g_ivf_index.build_time_seconds);
    send_fmt(fd, "OK");
}

void handle_client(int fd, const server_config_t *cfg) {
    int rfd = dup(fd);
    if (rfd < 0) {
        cerr << "handle_client: dup: " << strerror(errno) << endl;
        send_fmt(fd, "ERR internal error (dup failed)");
        return;
    }

    __gnu_cxx::stdio_filebuf<char> filebuf(rfd, ios::in);
    istream in(&filebuf);
    string line;

    while (getline(in, line)) {
        size_t len = line.size();
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        line.resize(len);
        if (len == 0) continue;

        size_t pos = line.find_first_of(" \t");
        string keyword, rest;
        if (pos == string::npos) { keyword = line; rest = ""; }
        else { keyword = line.substr(0, pos); rest = line.substr(pos + 1); }

        if      (keyword == "ADD")    cmd_add(fd, cfg, rest);
        else if (keyword == "SEARCH") cmd_search(fd, cfg, rest);
        else if (keyword == "BUILD")  cmd_build(fd, cfg);
        else if (keyword == "STATS")  cmd_stats(fd, cfg);
        else if (keyword == "QUIT")   { send_fmt(fd, "BYE"); break; }
        else                          send_fmt(fd, "ERR unknown command '%s'", keyword.c_str());
    }

    filebuf.close();
}