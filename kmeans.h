#ifndef KMEANS_H
#define KMEANS_H
#include "metric.h"        // MetricType
#include "vector_store.h"
#include <vector>

#define KMEANS_MAX_ITERATIONS 50

struct kmeans_result {
    int k = 0;
    int dim = 0;
    int iterations = 0;
    std::vector<float> centroids;      // flat array: centroid c starts at c * dim
    std::vector<int>   assignments;    // assignments[i] = cluster index for vector i
    std::vector<int>   cluster_sizes;  // cluster_sizes[c] may be zero for empty clusters
};

// Run Lloyd's k-means over the vectors in `store`.
// metric controls which distance function is used in the assignment step.
// Defaults to EUCLIDEAN so existing call-sites need no changes.
int kmeans_cluster(const vector_store_t& store,
                   kmeans_result&        out,
                   int                   max_iterations = KMEANS_MAX_ITERATIONS,
                   MetricType            metric         = MetricType::EUCLIDEAN);
#endif // KMEANS_H
