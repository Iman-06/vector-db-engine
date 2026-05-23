#include "kmeans.h"
#include "vdb_interface.h"
#include <algorithm>
#include <cmath>
#include <numeric> 
#include <random>
using namespace std;

static const float* centroid_ptr(const vector<float>& centroids, int cluster, int dim)
// Returns a pointer to the centroid vector for the given cluster index.
{
    return centroids.data() + static_cast<size_t>(cluster) * static_cast<size_t>(dim);
}

static int nearest_centroid(const float* vec, const vector<float>& centroids,
                            int k, int dim, MetricType metric)
// Finds the nearest centroid to the given vector using the active metric.
// compute_distance() dispatches to vdb_dist_sq (Euclidean) or
// cosine_distance (Cosine) based on the metric flag.
{
    int best_cluster = 0;
    double best_dist = compute_distance(vec, centroid_ptr(centroids, 0, dim), dim, metric);

    for (int c = 1; c < k; c++) {
        double dist = compute_distance(vec, centroid_ptr(centroids, c, dim), dim, metric);
        if (dist < best_dist) {
            best_dist = dist;
            best_cluster = c;
        }
    }

    return best_cluster;
}

static void reset_result(kmeans_result& out, int k, int dim, size_t n)
// Initializes the kmeans_result structure for a new clustering run.
{
    out.k = k;
    out.dim = dim;
    out.iterations = 0;
    out.centroids.assign(static_cast<size_t>(k) * static_cast<size_t>(dim), 0.0f); 
    out.assignments.assign(n, -1);
    out.cluster_sizes.assign(static_cast<size_t>(k), 0);
}

static void initialize_centroids(const vector_store_t& store, kmeans_result& out)
// Initializes centroids by randomly selecting `k` vectors from the store.
{
    const size_t n = store.count;
    vector<size_t> indices(n);
    iota(indices.begin(), indices.end(), 0);// Fill with 0, 1, ..., n-1

    random_device rd;
    mt19937 gen(rd());// Shuffle the indices to get random samples
    shuffle(indices.begin(), indices.end(), gen);

    for (int c = 0; c < out.k; c++) {
        const float* vec = vs_get_vector(&store, indices[static_cast<size_t>(c)]);// Get a pointer to the randomly selected vector
        float* centroid = out.centroids.data() + static_cast<size_t>(c) * static_cast<size_t>(out.dim);// Get pointer to the centroid array for cluster c
        copy(vec, vec + out.dim, centroid);// Copy the selected vector into the centroid array
    }
}

static bool assign_vectors(const vector_store_t& store, kmeans_result& out,
                           MetricType metric)
// Assigns each vector in the store to the nearest centroid using `metric`.
// Returns true if any assignment changed (used for convergence detection).
{
    bool changed = false;
    fill(out.cluster_sizes.begin(), out.cluster_sizes.end(), 0);

    for (size_t i = 0; i < store.count; i++) {
        const float* vec = vs_get_vector(&store, i);
        int cluster = nearest_centroid(vec, out.centroids, out.k, out.dim, metric);

        if (out.assignments[i] != cluster) {
            out.assignments[i] = cluster;
            changed = true;
        }

        out.cluster_sizes[static_cast<size_t>(cluster)]++;
    }

    return changed;
}

static void recompute_centroids(const vector_store_t& store, kmeans_result& out)
// Recomputes the centroids as the mean of all vectors assigned to each cluster.
{
    vector<double> sums(static_cast<size_t>(out.k) * static_cast<size_t>(out.dim), 0.0);

    for (size_t i = 0; i < store.count; i++) {
        int cluster = out.assignments[i];
        const float* vec = vs_get_vector(&store, i);
        size_t base = static_cast<size_t>(cluster) * static_cast<size_t>(out.dim);

        for (int d = 0; d < out.dim; d++) {
            sums[base + static_cast<size_t>(d)] += vec[d];
        }
    }

    for (int c = 0; c < out.k; c++) {
        int cluster_size = out.cluster_sizes[static_cast<size_t>(c)];
        if (cluster_size == 0) {
            continue; // Empty clusters stay in the index with their old centroid.
        }

        size_t base = static_cast<size_t>(c) * static_cast<size_t>(out.dim);
        for (int d = 0; d < out.dim; d++) {
            out.centroids[base + static_cast<size_t>(d)] =
                static_cast<float>(sums[base + static_cast<size_t>(d)] / cluster_size);
        }
    }
}

static int kmeans_cluster_count(size_t vector_count)
// Heuristic to determine the number of clusters `k` based on the number of vectors.
{
    if (vector_count == 0) {
        return 0;
    }

    int k = static_cast<int>(floor(sqrt(static_cast<double>(vector_count))));
    return (k > 0) ? k : 1;
}

int kmeans_cluster(const vector_store_t& store, kmeans_result& out,
                   int max_iterations, MetricType metric)
// Lloyd's k-means.  Initializes centroids, then iterates:
//   assign each vector to its nearest centroid (using `metric`)
//   recompute centroids as the arithmetic mean of assigned vectors
// Stops when no assignment changes or max_iterations is reached.
// The centroid recomputation (mean) is metric-independent and unchanged.
{
    const size_t n = store.count;
    const int dim = store.dim;
    const int k = kmeans_cluster_count(n);

    reset_result(out, k, dim, n);

    if (n == 0) {
        return VS_OK;
    }

    if (max_iterations <= 0) {
        max_iterations = KMEANS_MAX_ITERATIONS;
    }
    if (max_iterations > KMEANS_MAX_ITERATIONS) {
        max_iterations = KMEANS_MAX_ITERATIONS;
    }

    initialize_centroids(store, out);

    for (int iter = 0; iter < max_iterations; iter++) {
        // Assignment step: uses compute_distance(metric) internally
        bool changed = assign_vectors(store, out, metric);
        // Update step: arithmetic mean — same formula regardless of metric
        recompute_centroids(store, out);
        out.iterations = iter + 1;

        if (!changed) {
            break;
        }
    }

    return VS_OK;
}
