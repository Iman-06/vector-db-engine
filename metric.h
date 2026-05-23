// metric.h — Distance metric abstraction for the Vector DB Engine.
//
// This header sits at the bottom of the include chain so every module
// (kmeans, ivf, search, command, server) can see MetricType without
// creating circular dependencies.
//
// Two metrics are supported:
//   EUCLIDEAN — squared L2 distance (fast, avoids sqrt; ordering is preserved)
//   COSINE    — 1 − cosine_similarity; range [0, 2], lower = more similar
//
// The formula for cosine distance:
//   1 − (a · b) / (‖a‖ · ‖b‖)
// where a · b is the dot product and ‖a‖ is the L2 magnitude of a.
// A value of 0 means the vectors point in the same direction (identical
// angle); a value of 2 means they point in exactly opposite directions.

#ifndef METRIC_H
#define METRIC_H

#include <cmath>

// ── Metric selector ──────────────────────────────────────────────────────────

// Selects which distance function the engine uses.
// Passed as a server start-up flag (--metric euclidean|cosine) and stored
// in server_config_t, ivf_index_t, and threaded through all distance calls.
enum class MetricType {
    EUCLIDEAN,   // default — squared L2, no sqrt needed
    COSINE       // 1 - dot(a,b)/(|a|*|b|)
};

// ── Distance functions ───────────────────────────────────────────────────────

// Squared Euclidean distance: Σ (a[i] - b[i])²
// Skipping sqrt is safe because we only compare distances (monotonic).
inline double vdb_dist_sq(const float* a, const float* b, int dim)
{
    double sum = 0.0;
    for (int i = 0; i < dim; i++) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return sum;
}

// Cosine distance: 1 − (a · b) / (‖a‖ · ‖b‖)
// Returns 1.0 (maximum dissimilarity) when either vector is the zero vector.
inline double cosine_distance(const float* a, const float* b, int dim)
{
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < dim; i++) {
        double ai = static_cast<double>(a[i]);
        double bi = static_cast<double>(b[i]);
        dot += ai * bi;
        na  += ai * ai;
        nb  += bi * bi;
    }
    // Guard: zero-magnitude vector has no defined angle → treat as maximally
    // dissimilar so it never wins a nearest-neighbour search.
    if (na == 0.0 || nb == 0.0) return 1.0;
    return 1.0 - dot / std::sqrt(na * nb);
}

// Unified dispatcher — routes to the correct distance function.
// Used by search, k-means, and IVF insertion so every module calls one
// function instead of branching individually.
inline double compute_distance(const float* a, const float* b,
                               int dim, MetricType metric)
{
    return (metric == MetricType::COSINE)
        ? cosine_distance(a, b, dim)
        : vdb_dist_sq(a, b, dim);
}

#endif // METRIC_H
