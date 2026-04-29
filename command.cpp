// command.cpp: Command parsing, dispatch, and response formatting.
#define _POSIX_C_SOURCE 200809L

#include "command.h"
#include "vdb_interface.h"
#include "vector_store.h"

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

/* Maximum bytes in a single command line (long SEARCH lines can be large) */
#define MAX_LINE  8192

/* Upper bound on k to guard against absurd allocations */
#define MAX_K     10000

/*send_fmt — format a string and write it to the client socket.
  We write with a single write() call so that short responses are not split across multiple TCP segments by Nagle's algorithm.
 */
static void send_fmt(int fd, const char *fmt, ...) {
    char  buf[MAX_LINE + 4];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);// this is like printf, but: Writes into buf instead of printing -2 leaves space for \n and null terminator. returns the num of char written
    va_end(ap); // cleans va_list ap
    if (n <= 0) return;
    if (buf[n - 1] != '\n') { buf[n] = '\n'; n++; }
    write(fd, buf, (size_t)n);
}


//cmd_add: handle: ADD <id> <v1> <v2> ... <vD>
static void cmd_add(int fd, const server_config_t *cfg, string rest) {
    const int  dim = cfg->dim;
    string tok;
    char  *endp;

    stringstream ss(rest);

    //parse id
    if (!(ss >> tok)) {
        send_fmt(fd, "ERR ADD: missing id");
        return;
    }
    int64_t id = strtoll(tok.c_str(), &endp, 10);
    if (*endp != '\0') {
        send_fmt(fd, "ERR ADD: bad id '%s' (must be an integer)", tok.c_str());
        return;
    }

    // parse dim float components 
    vector<float> vec((size_t)dim);

    for (int i = 0; i < dim; i++) {
        if (!(ss >> tok)) {
            send_fmt(fd, "ERR ADD: expected %d components, got %d", dim, i);
            return;
        }
        vec[i] = strtof(tok.c_str(), &endp);
        if (*endp != '\0') {
            send_fmt(fd, "ERR ADD: bad float '%s' at component %d", tok.c_str(), i);
            return;
        }
    }

    // reject extra tokens 
    if (ss >> tok) {
        send_fmt(fd, "ERR ADD: too many values (server dim = %d)", dim);
        return;
    }

    //store
    int rc = vs_add(cfg->store, id, vec.data());

    switch (rc) {
        case VS_OK:        send_fmt(fd, "OK");                        break;
        case VS_ERR_NOMEM: send_fmt(fd, "ERR ADD: out of memory");    break;
        default:           send_fmt(fd, "ERR ADD: store error %d", rc); break;
    }
}

/* 
 cmd_search — handle: SEARCH <v1> ... <vD> <k> BRUTE
 WHY we snapshot vectors while holding the lock:
   We need to print each result's float components. vs_get_vector()
   returns a raw pointer into the store's internal array. If we released
   the lock before reading those floats another thread could realloc the
   array and our pointer would dangle. Snapshotting under the lock is safe.
 */
static void cmd_search(int fd, const server_config_t *cfg, string rest)
{
    const int  dim = cfg->dim;
    string tok;
    char      *endp;

    stringstream ss(rest);

    // parse query vector 
    vector<float> query((size_t)dim);
    for (int i = 0; i < dim; i++) {
        if (!(ss >> tok)) {
            send_fmt(fd, "ERR SEARCH: expected %d query components, got %d", dim, i);
            return;
        }
        query[i] = strtof(tok.c_str(), &endp);
        if (*endp != '\0') {
            send_fmt(fd, "ERR SEARCH: bad float '%s' at component %d", tok.c_str(), i);
            return;
        }
    }

    // parse k
    if (!(ss >> tok)) {
        send_fmt(fd, "ERR SEARCH: missing k");
        return;
    }
    long k = strtol(tok.c_str(), &endp, 10);
    if (*endp != '\0' || k <= 0 || k > MAX_K) {
        send_fmt(fd, "ERR SEARCH: bad k '%s' (must be 1..%d)", tok.c_str(), MAX_K);
        return;
    }

    // parse mode
    if (!(ss >> tok)) {
        send_fmt(fd, "ERR SEARCH: missing mode (expected BRUTE)");
        return;
    }
    search_mode_t mode;
    if (tok == "BRUTE") {
        mode = search_mode_t::SEARCH_MODE_BRUTE;
    } else {
        send_fmt(fd, "ERR SEARCH: unknown mode '%s' (Phase 1 supports BRUTE)", tok.c_str());
        return;
    }

    // allocate result array
    vector<search_result_t> results((size_t)k);

    // lock store -> search -> snapshot vectors -> unlock.
    vs_lock(cfg->store);

    size_t scanned   = cfg->store->count;   //vectors in store right now 
    int    out_count = 0;
    int    rc        = search_brute(*cfg->store, query, (int)k, results, out_count);
    /*
     * While still locked, copy each result vector's float components into a
     * flat snapshot buffer.  This lets umakes safely print them after unlock.
     */
    vector<float> snap;
    if (rc == VS_OK && out_count > 0) {
        snap.resize((size_t)out_count * (size_t)dim);// this is the snapshot buffer for the result vectors. It is a flat array where each result's vector components are stored contiguously
        for (int i = 0; i < out_count; i++) {
            const float *v = vs_get_vector(cfg->store, results[i].store_index);
            if (v)
            {memcpy(snap.data() + (size_t)i * (size_t)dim, v, (size_t)dim * sizeof(float));}
        }
    }


    vs_unlock(cfg->store);

    if (rc != VS_OK) {
        send_fmt(fd, "ERR SEARCH: search failed (rc=%d)", rc);
        return;
    }

    const char *mode_str = (mode == search_mode_t::SEARCH_MODE_BRUTE) ? "BRUTE" : "ANN";

    // send one result line per hit
    for (int i = 0; i < out_count; i++) {
        ostringstream line;

        // start: id and distance
        line.setf(ios::fixed); // use fixed-point notation for floats
        line.precision(6); // 6 decimal places for float components
        line << (long long)results[i].id << " " << results[i].distance;

        // append each vector component
        if (!snap.empty()) {
            const float *v = snap.data() + (size_t)i * (size_t)dim;
            for (int j = 0; j < dim; j++) {
                // Guard against absurdly long lines if dim is huge.  We still send the id and distance, just truncate the vector components.
                if ((int)line.str().size() >= MAX_LINE - 16) break;
                line << " " << v[j];
            }
        }

        // ensure newline
        string out = line.str() + "\n";
        write(fd, out.c_str(), out.size());
    }

    // summary line
    send_fmt(fd, "(%d results, mode = %s, scanned %zu)",
             out_count, mode_str, scanned);
}

// cmd_stats handle: STATS
static void cmd_stats(int fd, const server_config_t *cfg) {
    vs_lock(cfg->store);
    int    dim   = cfg->store->dim;
    size_t count = cfg->store->count;
    vs_unlock(cfg->store);

    send_fmt(fd, "dim %d",       dim);
    send_fmt(fd, "vectors %zu",  count);
    send_fmt(fd, "index_built 0");
    send_fmt(fd, "clusters N/A");
}

void handle_client(int fd, const server_config_t *cfg) {
    // dup so that filebuf.close() does not close the original write fd
    int rfd = dup(fd);
    if (rfd < 0) {
        cerr << "handle_client: dup: " << strerror(errno) << endl;
        send_fmt(fd, "ERR internal error (dup failed)");
        return;
    }

    __gnu_cxx::stdio_filebuf<char> filebuf(rfd, ios::in);// converts socket/file descriptor into C++ stream buffer
    istream in(&filebuf); 

    string line;

    while (getline(in, line)) {

        //strip trailing CR / LF 
        size_t len = line.size();
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        line.resize(len);

        if (len == 0) continue;   // blank line — ignore silently

        size_t pos = line.find_first_of(" \t");
        string rest;
        string keyword;

        if (pos == string::npos) {
            keyword = line;
            rest = "";
        } else {
            keyword = line.substr(0, pos);   // null-terminate the keyword
            rest = line.substr(pos + 1);     // rest now points to the first argument char
        }
        // `line` now holds only the keyword; `rest` holds the arguments

        //  dispatch
        if (keyword == "ADD") {
            cmd_add(fd, cfg, rest);

        } else if (keyword == "SEARCH") {
            cmd_search(fd, cfg, rest);

        } else if (keyword == "STATS") {
            cmd_stats(fd, cfg);

        } else if (keyword == "QUIT") {
            send_fmt(fd, "BYE");
            break;   //exit loop — server keeps running, only this client disconnects 

        } else {
            send_fmt(fd, "ERR unknown command '%s'", keyword.c_str());
        }
    }

    filebuf.close();

}
