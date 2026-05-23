#ifndef IVF_H
#define IVF_H
#include "metric.h"        // MetricType
#include "vector_store.h"
#include "kmeans.h"
#include <vector>
#include <mutex>           // mutex for multithreaded access to the index
#include <cstdint>
using namespace std;

struct ivf_index_t {
    bool   built  = false;
    int    k      = 0;
    int    dim    = 0;
    int    iterations         = 0;      // how many k-means iterations ran
    double build_time_seconds = 0.0;    // wall time for BUILD
    // Distance metric the index was built with.
    // Stored here so ivf_insert() and search_ivf() are self-contained —
    // they never need the metric passed separately at query time.
    MetricType metric = MetricType::EUCLIDEAN;
    vector<float>        centroids;     // flat: centroid c starts at c * dim
    vector<vector<int>>  clusters;      // clusters[c] = internal store indices
};

// Build the IVF index from the current store contents using k-means.
// metric is stored in the resulting index and used for all future searches
// and incremental inserts.  Caller must hold the store mutex.
// Defaults to EUCLIDEAN so existing call-sites compile unchanged.
int ivf_build(const vector_store_t& store,
              ivf_index_t&          index,
              MetricType            metric = MetricType::EUCLIDEAN);

// Insert a newly added vector (at internal store index new_idx) into the
// nearest cluster.  Uses index.metric — no extra parameter needed.
// Caller must hold the store mutex.
void ivf_insert(ivf_index_t& index, const float* vec, int new_idx);

#endif // IVF_H