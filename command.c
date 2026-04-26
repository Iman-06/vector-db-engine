/*
 * command.c — Command parsing, dispatch, and response formatting.
 * Member 1 owns this file.
 *
 * SUPPORTED COMMANDS (Phase 1):
 *
 *   ADD <id> <v1> <v2> ... <vD>
 *     Store a vector.  Overwrites if id already exists.
 *     Response: "OK"  or  "ERR <reason>"
 *
 *   SEARCH <v1> <v2> ... <vD> <k> BRUTE
 *     Find the k nearest neighbours of the query vector.
 *     Response: one line per result — "<id> <dist> <v1> ... <vD>"
 *               then a summary line  — "(<N> results, mode = BRUTE, scanned <S>)"
 *
 *   STATS
 *     Print basic store statistics.
 *     Response: "dim <D>"  /  "vectors <N>"  /  "index_built <0|1>"  / ...
 *
 *   QUIT
 *     Disconnect this client only.  Server keeps running.
 *     Response: "BYE"
 *
 * THREAD SAFETY
 *   handle_client() is called from a dedicated pthread per client.
 *   We use strtok_r() (thread-safe) instead of strtok() everywhere.
 *   For SEARCH, we hold the store lock for the entire search + vector
 *   snapshot so the data cannot change under us mid-scan.
 *   For ADD, vs_add() manages its own lock internally (Member 2).
 *   For STATS, we lock briefly to snapshot the counters.
 */

#define _POSIX_C_SOURCE 200809L

#include "command.h"
#include "vdb_interface.h"
#include "vector_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

/* Maximum bytes in a single command line (long SEARCH lines can be large) */
#define MAX_LINE  8192

/* Upper bound on k to guard against absurd allocations */
#define MAX_K     10000

/* ──────────────────────────────────────────────────────────────────────
 * send_fmt — format a string and write it to the client socket.
 *
 * Appends '\n' if the formatted string does not already end with one.
 * We write with a single write() call so that short responses are not
 * split across multiple TCP segments by Nagle's algorithm.
 * ────────────────────────────────────────────────────────────────────── */
static void send_fmt(int fd, const char *fmt, ...) {
    char    buf[MAX_LINE + 4];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    /* ensure exactly one trailing newline */
    if (buf[n - 1] != '\n') { buf[n] = '\n'; n++; }
    write(fd, buf, (size_t)n);
}

/* ──────────────────────────────────────────────────────────────────────
 * cmd_add — handle: ADD <id> <v1> <v2> ... <vD>
 *
 * 1. Parse the integer id.
 * 2. Parse exactly cfg->dim float components.
 * 3. Call vs_add() (Member 2) — that function handles its own mutex.
 * 4. Send "OK" or "ERR <reason>".
 *
 * `rest` points to the part of the line after "ADD ".
 * ────────────────────────────────────────────────────────────────────── */
static void cmd_add(int fd, const server_config_t *cfg, char *rest) {
    const int  dim = cfg->dim;
    char      *saveptr;
    char      *endp;

    /* ── parse id ── */
    char *tok = strtok_r(rest, " \t\r\n", &saveptr);
    if (!tok) {
        send_fmt(fd, "ERR ADD: missing id");
        return;
    }
    int64_t id = strtoll(tok, &endp, 10);
    if (*endp != '\0') {
        send_fmt(fd, "ERR ADD: bad id '%s' (must be an integer)", tok);
        return;
    }

    /* ── parse dim float components ── */
    float *vec = malloc((size_t)dim * sizeof(float));
    if (!vec) {
        send_fmt(fd, "ERR ADD: server out of memory");
        return;
    }

    for (int i = 0; i < dim; i++) {
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
        if (!tok) {
            send_fmt(fd, "ERR ADD: expected %d components, got %d", dim, i);
            free(vec);
            return;
        }
        vec[i] = strtof(tok, &endp);
        if (*endp != '\0') {
            send_fmt(fd, "ERR ADD: bad float '%s' at component %d", tok, i);
            free(vec);
            return;
        }
    }

    /* reject extra tokens */
    if (strtok_r(NULL, " \t\r\n", &saveptr) != NULL) {
        send_fmt(fd, "ERR ADD: too many values (server dim = %d)", dim);
        free(vec);
        return;
    }

    /* ── store ── */
    int rc = vs_add(cfg->store, id, vec);
    free(vec);

    switch (rc) {
        case VS_OK:        send_fmt(fd, "OK");                        break;
        case VS_ERR_NOMEM: send_fmt(fd, "ERR ADD: out of memory");    break;
        default:           send_fmt(fd, "ERR ADD: store error %d", rc); break;
    }
}

/* ──────────────────────────────────────────────────────────────────────
 * cmd_search — handle: SEARCH <v1> ... <vD> <k> BRUTE
 *
 * 1. Parse query vector (dim floats).
 * 2. Parse k (positive integer).
 * 3. Parse mode string ("BRUTE" only in Phase 1).
 * 4. Lock store, call search_brute() (Member 3), snapshot result vectors,
 *    unlock store.
 * 5. Send one result line per hit:    "<id> <dist> <v1> ... <vD>"
 *    Send summary line:               "(<N> results, mode = BRUTE, scanned <S>)"
 *
 * WHY we snapshot vectors while holding the lock:
 *   We need to print each result's float components. vs_get_vector()
 *   returns a raw pointer into the store's internal array. If we released
 *   the lock before reading those floats another thread could realloc the
 *   array and our pointer would dangle. Snapshotting under the lock is safe.
 * ────────────────────────────────────────────────────────────────────── */
static void cmd_search(int fd, const server_config_t *cfg, char *rest) {
    const int  dim = cfg->dim;
    char      *saveptr;
    char      *endp;

    /* ── parse query vector ── */
    float *query = malloc((size_t)dim * sizeof(float));
    if (!query) {
        send_fmt(fd, "ERR SEARCH: server out of memory");
        return;
    }

    char *tok = strtok_r(rest, " \t\r\n", &saveptr);
    for (int i = 0; i < dim; i++) {
        if (!tok) {
            send_fmt(fd, "ERR SEARCH: expected %d query components, got %d", dim, i);
            free(query);
            return;
        }
        query[i] = strtof(tok, &endp);
        if (*endp != '\0') {
            send_fmt(fd, "ERR SEARCH: bad float '%s' at component %d", tok, i);
            free(query);
            return;
        }
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    /* ── parse k ── */
    if (!tok) {
        send_fmt(fd, "ERR SEARCH: missing k");
        free(query);
        return;
    }
    long k = strtol(tok, &endp, 10);
    if (*endp != '\0' || k <= 0 || k > MAX_K) {
        send_fmt(fd, "ERR SEARCH: bad k '%s' (must be 1..%d)", tok, MAX_K);
        free(query);
        return;
    }

    /* ── parse mode ── */
    tok = strtok_r(NULL, " \t\r\n", &saveptr);
    if (!tok) {
        send_fmt(fd, "ERR SEARCH: missing mode (expected BRUTE)");
        free(query);
        return;
    }
    search_mode_t mode;
    if (strcmp(tok, "BRUTE") == 0) {
        mode = SEARCH_MODE_BRUTE;
    } else {
        send_fmt(fd, "ERR SEARCH: unknown mode '%s' (Phase 1 supports BRUTE)", tok);
        free(query);
        return;
    }

    /* ── allocate result array ── */
    search_result_t *results = malloc((size_t)k * sizeof(search_result_t));
    if (!results) {
        send_fmt(fd, "ERR SEARCH: server out of memory");
        free(query);
        return;
    }

    /* ────────────────────────────────────────────────────────────────
     * Critical section: lock store → search → snapshot vectors → unlock.
     * search_brute() must NOT call vs_lock() — the lock is already held.
     * ──────────────────────────────────────────────────────────────── */
    vs_lock(cfg->store);

    size_t scanned   = cfg->store->count;   /* vectors in store right now */
    int    out_count = 0;
    int    rc        = search_brute(cfg->store, query, (int)k, results, &out_count);

    /*
     * While still locked, copy each result vector's float components into a
     * flat snapshot buffer.  This lets us safely print them after unlock.
     */
    float *snap = NULL;
    if (rc == VS_OK && out_count > 0) {
        snap = malloc((size_t)out_count * (size_t)dim * sizeof(float));
        if (snap) {
            for (int i = 0; i < out_count; i++) {
                const float *v = vs_get_vector(cfg->store, results[i].store_index);
                if (v)
                    memcpy(snap + (size_t)i * (size_t)dim,
                           v,
                           (size_t)dim * sizeof(float));
            }
        }
    }

    vs_unlock(cfg->store);
    /* ── end critical section ── */

    free(query);

    if (rc != VS_OK) {
        send_fmt(fd, "ERR SEARCH: search failed (rc=%d)", rc);
        free(results);
        free(snap);
        return;
    }

    const char *mode_str = (mode == SEARCH_MODE_BRUTE) ? "BRUTE" : "ANN";

    /* ── send one result line per hit ── */
    for (int i = 0; i < out_count; i++) {
        char line[MAX_LINE];
        int  pos;

        /* start: id and distance */
        pos = snprintf(line, sizeof(line),
                       "%lld %.6f",
                       (long long)results[i].id,
                       results[i].distance);

        /* append each vector component */
        if (snap) {
            const float *v = snap + (size_t)i * (size_t)dim;
            for (int j = 0; j < dim; j++) {
                /* leave room for " -X.XXXXXX\n\0" (at least 14 chars) */
                if (pos >= (int)sizeof(line) - 16) break;
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                                " %.6f", v[j]);
            }
        }

        /* ensure newline */
        if (pos < (int)sizeof(line) - 1) { line[pos] = '\n'; pos++; }
        write(fd, line, (size_t)pos);
    }

    /* summary line */
    send_fmt(fd, "(%d results, mode = %s, scanned %zu)",
             out_count, mode_str, scanned);

    free(results);
    free(snap);
}

/* ──────────────────────────────────────────────────────────────────────
 * cmd_stats — handle: STATS
 *
 * Locks the store briefly to read a consistent snapshot, then sends:
 *   dim <D>
 *   vectors <N>
 *   index_built 0         (always 0 in Phase 1 — no index yet)
 *   clusters N/A          (Phase 2+ only)
 * ────────────────────────────────────────────────────────────────────── */
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

/* ──────────────────────────────────────────────────────────────────────
 * handle_client — main command loop for one connected client.
 *
 * We wrap the socket in a FILE* (via dup + fdopen) so we can use fgets()
 * for clean line-by-line reading.  We dup() the fd first because fclose()
 * will close the underlying fd; we keep the original fd open so we can
 * still write responses to it with write() / send_fmt().
 *
 * The function returns when:
 *   • The client sends QUIT (we send "BYE" then break).
 *   • The client closes the connection (fgets returns NULL / EOF).
 * ────────────────────────────────────────────────────────────────────── */
void handle_client(int fd, const server_config_t *cfg) {
    /* dup so that fclose() on `in` does not close the original write fd */
    int rfd = dup(fd);
    if (rfd < 0) {
        perror("handle_client: dup");
        send_fmt(fd, "ERR internal error (dup failed)");
        return;
    }

    FILE *in = fdopen(rfd, "r");
    if (!in) {
        perror("handle_client: fdopen");
        close(rfd);
        send_fmt(fd, "ERR internal error (fdopen failed)");
        return;
    }

    char line[MAX_LINE];

    while (fgets(line, (int)sizeof(line), in) != NULL) {

        /* strip trailing CR / LF */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;   /* blank line — ignore silently */

        /*
         * Split the line into command keyword and the rest of the arguments.
         *
         *   "ADD 1 0.1 0.2"  →  cmd = "ADD",    rest = "1 0.1 0.2"
         *   "STATS"          →  cmd = "STATS",  rest = ""  (empty string)
         *   "QUIT"           →  cmd = "QUIT",   rest = ""
         */
        char *rest = line;
        while (*rest && *rest != ' ' && *rest != '\t') rest++;
        if (*rest) {
            *rest = '\0';   /* null-terminate the keyword */
            rest++;         /* rest now points to the first argument char */
        }
        /* `line` now holds only the keyword; `rest` holds the arguments */

        /* ── dispatch ── */
        if (strcmp(line, "ADD") == 0) {
            cmd_add(fd, cfg, rest);

        } else if (strcmp(line, "SEARCH") == 0) {
            cmd_search(fd, cfg, rest);

        } else if (strcmp(line, "STATS") == 0) {
            cmd_stats(fd, cfg);

        } else if (strcmp(line, "QUIT") == 0) {
            send_fmt(fd, "BYE");
            break;   /* exit loop — server keeps running, only this client disconnects */

        } else {
            send_fmt(fd, "ERR unknown command '%s'", line);
        }
    }

    /* fclose also closes rfd (the dup'd descriptor) */
    fclose(in);
    /* The original fd is closed by client_thread() in server.c */
}
