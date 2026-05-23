#include "ivf.h"
#include "kmeans.h"
#include "vector_store.h"
#include "vdb_interface.h"
#include <cmath>
#include <chrono> // for timing the build
#include <cstring>
using namespace std;

int ivf_build(const vector_store_t& store, ivf_index_t& index, MetricType metric) {
    auto t_start = chrono::steady_clock::now();

    // Run k-means with the chosen metric.
    // The metric controls which distance function is used in the
    // assignment step (nearest centroid per vector).
    kmeans_result km;
    int rc = kmeans_cluster(store, km, KMEANS_MAX_ITERATIONS, metric);
    if (rc != VS_OK) return rc;

    index.built      = true;
    index.k          = km.k;
    index.dim        = km.dim;
    index.iterations = km.iterations;
    index.centroids  = km.centroids;
    // Store the metric so ivf_insert() and search_ivf() stay self-contained:
    // they read index.metric instead of needing it passed at every call.
    index.metric     = metric;

    // Build per-cluster membership lists from k-means assignments
    index.clusters.clear();
    index.clusters.resize((size_t)km.k);
    for (size_t i = 0; i < store.count; i++) {
        int cluster = km.assignments[i];
        index.clusters[(size_t)cluster].push_back((int)i);
    }

    auto t_end = chrono::steady_clock::now();
    chrono::duration<double> elapsed = t_end - t_start;
    index.build_time_seconds = elapsed.count();
    return VS_OK;
}

void ivf_insert(ivf_index_t& index, const float* vec, int new_idx) {
    if (!index.built || index.k == 0) return;

    // Use index.metric so insertion is consistent with how the index was built.
    // No extra parameter needed — the metric is self-contained in the index.
    int best_cluster = 0;
    double best_dist = compute_distance(vec, index.centroids.data(),
                                        index.dim, index.metric);
    for (int c = 1; c < index.k; c++) {
        const float* cent = index.centroids.data() + (size_t)c * (size_t)index.dim;
        double dist = compute_distance(vec, cent, index.dim, index.metric);
        if (dist < best_dist) {
            best_dist = dist;
            best_cluster = c;
        }
    }
    index.clusters[(size_t)best_cluster].push_back(new_idx);
}