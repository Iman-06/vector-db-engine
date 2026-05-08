#include "ivf.h"
#include "kmeans.h"
#include "vector_store.h"
#include "vdb_interface.h"
#include <cmath>
#include <chrono> // for timing the build
#include <cstring>
using namespace std;

int ivf_build(const vector_store_t& store, ivf_index_t& index) { 
    auto t_start =  chrono::steady_clock::now(); 
    // Run k-means store mutex already held by caller
    kmeans_result km;
    int rc = kmeans_cluster(store, km);   // return code from kmeans_cluster
    if (rc != VS_OK) return rc;
    index.built      = true;
    index.k          = km.k;
    index.dim        = km.dim;
    index.iterations = km.iterations;
    index.centroids  = km.centroids;      
    // Build per-cluster lists from assignments
    index.clusters.clear();
    index.clusters.resize((size_t)km.k);
    for (size_t i = 0; i < store.count; i++) {
        int cluster = km.assignments[i];
        index.clusters[(size_t)cluster].push_back((int)i); 
    }
    // --- Record elapsed time ---
    auto t_end =  chrono::steady_clock::now();
    chrono::duration<double> elapsed = t_end - t_start;
    index.build_time_seconds = elapsed.count();
    return VS_OK;
}

void ivf_insert(ivf_index_t& index, const float* vec, int new_idx) {
    if (!index.built || index.k == 0) return;
    int best_cluster = 0;
    double best_dist = vdb_dist_sq(vec,index.centroids.data(),index.dim);
    for (int c = 1; c < index.k; c++) {
        const float* cent = index.centroids.data() + (size_t)c * (size_t)index.dim;
        double dist = vdb_dist_sq(vec, cent, index.dim);
        if (dist < best_dist) {
            best_dist = dist;
            best_cluster = c;
        }
    }
    index.clusters[(size_t)best_cluster].push_back(new_idx);
}