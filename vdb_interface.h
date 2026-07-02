#ifndef VDB_INTERFACE_H
#define VDB_INTERFACE_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>
#include <string>
#include "vector_store.h"
#include "kmeans.h"
#include "ivf.h"
#include "metric.h" 

extern ivf_index_t g_ivf_index; 
using namespace std;

// Keep metric_t as an alias for MetricType
using metric_t = MetricType;

// Keep vdb_dist as an alias for compute_distance
inline double vdb_dist(const float* a, const float* b, int dim, metric_t metric) {
    return compute_distance(a, b, dim, metric);
}

// Keep vdb_dist_cosine as an alias for cosine_distance
inline double vdb_dist_cosine(const float* a, const float* b, int dim) {
    return cosine_distance(a, b, dim);
}

// Extra error codes
#define VDB_ERR_BADCMD   -10
#define VDB_ERR_BADARGS  -11
#define VDB_ERR_SEARCH   -12

enum class search_mode_t {
    SEARCH_MODE_BRUTE = 0,
    SEARCH_MODE_ANN   = 1
};

struct search_result_t {
    int64_t id       = 0;
    double  distance = 0.0;
    size_t  store_index = 0;
};

struct server_config_t {
    int    dim       = 0;
    int    port      = 0;
    string data_path;
    vector_store_t* store  = nullptr;
    metric_t        metric = metric_t::EUCLIDEAN;
};

// Search functions
int search_brute(const vector_store_t& vs,
                 const vector<float>& query,
                 int k,
                 vector<search_result_t>& out_results,
                 int& out_count,
                 metric_t metric = metric_t::EUCLIDEAN);

int search_ivf(const vector_store_t& vs,
               const ivf_index_t& ivf,
               const vector<float>& query,
               int k,
               int nprobe,
               vector<search_result_t>& out_results,
               int& out_count,
               size_t& out_scanned,
               metric_t metric = metric_t::EUCLIDEAN);

#endif