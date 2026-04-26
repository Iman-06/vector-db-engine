/*
 * vdb_interface.h — Shared interface for the Vector DB Engine.
 *
 * Member 1 defines this header. Members 2 and 3 implement their parts
 * against it. Every cross-member function call goes through here so there
 * is exactly one place to look up names, parameter order, and return codes.
 *
 * HOW TO USE
 *   Member 1 (server / command dispatch): #include "vdb_interface.h"
 *   Member 2 (vector store):              already in vector_store.h;
 *                                         #include "vdb_interface.h" for shared types
 *   Member 3 (search + client):           implement search_brute() declared below
 *                                         and #include "vdb_interface.h"
 */

#ifndef VDB_INTERFACE_H
#define VDB_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
#include "vector_store.h"   /* pulls in vector_store_t, VS_OK, VS_ERR_* */

/* ──────────────────────────────────────────────────────────────────────
 * Extra error codes (the VS_* codes from vector_store.h cover store errors)
 * ────────────────────────────────────────────────────────────────────── */
#define VDB_ERR_BADCMD   -10   /* unrecognised or malformed command      */
#define VDB_ERR_BADARGS  -11   /* wrong number / type of arguments       */
#define VDB_ERR_SEARCH   -12   /* search function reported a failure     */

/* ──────────────────────────────────────────────────────────────────────
 * Search modes — Phase 1 only uses BRUTE; ANN is reserved for Phase 2+
 * ────────────────────────────────────────────────────────────────────── */
typedef enum {
    SEARCH_MODE_BRUTE = 0,   /* exhaustive linear scan (k-NN)           */
    SEARCH_MODE_ANN   = 1    /* approximate nearest neighbour (Phase 2+) */
} search_mode_t;

/* ──────────────────────────────────────────────────────────────────────
 * search_result_t — one entry returned by any search function.
 *
 * store_index: the slot number inside vs->vectors / vs->ids where this
 *   vector lives.  Member 3 MUST fill this field so Member 1 can retrieve
 *   the original float components for the response line without doing a
 *   slow ID scan.  Use vs_get_vector(vs, store_index) to read the data.
 * ────────────────────────────────────────────────────────────────────── */
typedef struct {
    int64_t  id;           /* the vector's stored ID                    */
    double   distance;     /* Euclidean distance from query to this vec  */
    size_t   store_index;  /* slot index — fill this in search_brute()  */
} search_result_t;

/* ──────────────────────────────────────────────────────────────────────
 * server_config_t — runtime settings parsed from command-line arguments.
 *
 * Shared (read-only after startup) across all client threads.
 * ────────────────────────────────────────────────────────────────────── */
typedef struct {
    int              dim;            /* vector dimension  (--dim)        */
    int              port;           /* TCP listen port   (--port)       */
    char             data_path[256]; /* data directory    (--data)       */
    vector_store_t  *store;          /* live vector store (shared/locked) */
} server_config_t;

/* ──────────────────────────────────────────────────────────────────────
 * MEMBER 3 — search_brute()
 *
 * Find the k nearest neighbours of `query` by scanning every vector in
 * the store (brute-force / exhaustive search).
 *
 * CONTRACT FOR MEMBER 3:
 *   • Do NOT call vs_lock() / vs_unlock() inside this function.
 *     The caller (command.c) already holds the lock for the duration of
 *     the call.  Calling lock again on the same mutex will deadlock.
 *   • Fill out_results[0..(*out_count)-1] sorted by distance ascending
 *     (closest first).
 *   • Set out_results[i].store_index to the slot index in vs->vectors
 *     so Member 1 can print the vector's components.
 *   • If the store has fewer than k vectors, return however many exist
 *     and set *out_count accordingly.
 *
 * PARAMETERS:
 *   vs          pointer to the vector store (read-only; lock held by caller)
 *   query       float array of exactly vs->dim components
 *   k           number of nearest neighbours requested (>0)
 *   out_results caller-allocated array of at least k search_result_t entries
 *   out_count   set to the actual number of results written (<= k)
 *
 * RETURNS: VS_OK (0) on success, negative error code on failure.
 * ────────────────────────────────────────────────────────────────────── */
int search_brute(const vector_store_t *vs,
                 const float          *query,
                 int                   k,
                 search_result_t      *out_results,
                 int                  *out_count);

#endif /* VDB_INTERFACE_H */
