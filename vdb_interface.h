/*
 vdb_interface.h — Shared interface for the Vector DB Engine.
 Every cross-member function call goes through here so there is exactly one
 place to look up names, parameter order, and return codes.

 Distance metric support:
   Include metric.h first (it defines MetricType, vdb_dist_sq,
   cosine_distance, compute_distance).  MetricType is stored in
   server_config_t and propagated to every distance call at runtime.
 */

#ifndef VDB_INTERFACE_H
#define VDB_INTERFACE_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "metric.h"        // MetricType, vdb_dist_sq, compute_distance
#include "vector_store.h"
#include "kmeans.h"
#include "ivf.h"
extern ivf_index_t g_ivf_index;
using namespace std;

// Extra error codes
#define VDB_ERR_BADCMD   -10   /* unrecognised or malformed command  */
#define VDB_ERR_BADARGS  -11   /* wrong number / type of arguments   */
#define VDB_ERR_SEARCH   -12   /* search function reported a failure */

enum class search_mode_t {
    SEARCH_MODE_BRUTE = 0,
    SEARCH_MODE_ANN   = 1
};

// search_result_t — one entry returned by any search function.
struct search_result_t {
    int64_t id = 0;
    double  distance = 0.0;
    size_t  store_index = 0;
};

// server_config_t — runtime settings parsed from command-line arguments.
// metric defaults to EUCLIDEAN so existing code that constructs
// server_config_t{} continues to behave identically.
struct server_config_t {
    int    dim  = 0;
    int    port = 0;
    string data_path;
    vector_store_t* store  = nullptr;
    MetricType      metric = MetricType::EUCLIDEAN;   // NEW: --metric flag
};

// ── Search declarations ───────────────────────────────────────────────────────

// Brute-force: scans all N vectors with compute_distance(metric).
// metric defaults to EUCLIDEAN for backward compatibility.
int search_brute(const vector_store_t& vs,
                 const vector<float>&  query,
                 int                   k,
                 vector<search_result_t>& out_results,
                 int&                  out_count,
                 MetricType            metric = MetricType::EUCLIDEAN);

// IVF approximate search: metric is read from ivf.metric (self-contained).
int search_ivf(const vector_store_t& vs,
               const ivf_index_t&    ivf,
               const vector<float>&  query,
               int                   k,
               int                   nprobe,
               vector<search_result_t>& out_results,
               int&                  out_count,
               size_t&               out_scanned);

#endif // VDB_INTERFACE_H