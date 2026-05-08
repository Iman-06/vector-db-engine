#ifndef KMEANS_H
#define KMEANS_H
#include "vector_store.h"
#include <vector>

#define KMEANS_MAX_ITERATIONS 50
struct kmeans_result {
    int k = 0;
    int dim = 0;
    int iterations = 0;
    std::vector<float> centroids;      // flat array: centroid c starts at c * dim
    std::vector<int> assignments;      // assignments[i] is the cluster for vector i
    std::vector<int> cluster_sizes;    // cluster_sizes[c] may be zero for empty clusters
};

//Run Lloyd's k-means over the vectors already stored in `store`.
int kmeans_cluster(const vector_store_t& store, kmeans_result& out, int max_iterations = KMEANS_MAX_ITERATIONS);
#endif
