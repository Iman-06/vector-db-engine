// test_member2.cpp — Member 2: Normalization + Cosine distance tests
//
// Build:
//   g++ -std=c++17 -pthread -Wall -g \
//       test_member2.cpp vector_store.cpp normalize.cpp ivf.cpp kmeans.cpp search.cpp \
//       -o test_member2
// Run:
//   ./test_member2

#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include "vector_store.h"
#include "normalize.h"
#include "vdb_interface.h"

using namespace std;

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void run_test(const char* name, void(*fn)()) {
    tests_run++;
    try {
        fn();
        tests_passed++;
        cout << "  [PASS] " << name << "\n";
    } catch (const exception& e) {
        tests_failed++;
        cout << "  [FAIL] " << name << " — " << e.what() << "\n";
    }
}

#define ASSERT(cond) \
    do { if (!(cond)) throw runtime_error("failed: " #cond " (line " + to_string(__LINE__) + ")"); } while(0)
#define ASSERT_NEAR(a, b, eps) \
    do { if (fabs((double)(a)-(double)(b)) > (eps)) \
        throw runtime_error("not close: " + to_string(a) + " vs " + to_string(b) + " (line " + to_string(__LINE__) + ")"); } while(0)
#define ASSERT_EQ(a,b) \
    do { if ((a)!=(b)) throw runtime_error("expected " + to_string(b) + " got " + to_string(a) + " (line " + to_string(__LINE__) + ")"); } while(0)

// ═══════════════════════════════════════════════════════
// 1. NORMALIZATION TESTS
// ═══════════════════════════════════════════════════════

static void test_norm_unit_length_after_normalize() {
    // After normalizing, magnitude must be exactly 1.0
    float v[] = {3.0f, 4.0f};   // magnitude = 5
    ASSERT_EQ(normalize_vector(v, 2), NORM_OK);
    double mag = sqrt((double)v[0]*v[0] + (double)v[1]*v[1]);
    ASSERT_NEAR(mag, 1.0, 1e-6);
}

static void test_norm_values_correct() {
    // (3,4) normalized = (0.6, 0.8)
    float v[] = {3.0f, 4.0f};
    normalize_vector(v, 2);
    ASSERT_NEAR(v[0], 0.6f, 1e-6);
    ASSERT_NEAR(v[1], 0.8f, 1e-6);
}

static void test_norm_already_unit() {
    // A vector already unit length stays unit length
    float v[] = {1.0f, 0.0f, 0.0f};
    ASSERT_EQ(normalize_vector(v, 3), NORM_OK);
    ASSERT_NEAR(v[0], 1.0f, 1e-6);
    ASSERT_NEAR(v[1], 0.0f, 1e-6);
}

static void test_norm_zero_vector_rejected() {
    // Zero vector must return NORM_ERR_ZERO
    float v[] = {0.0f, 0.0f, 0.0f};
    ASSERT_EQ(normalize_vector(v, 3), NORM_ERR_ZERO);
}

static void test_norm_negative_components() {
    // (-3, -4) normalized = (-0.6, -0.8), magnitude still 1
    float v[] = {-3.0f, -4.0f};
    ASSERT_EQ(normalize_vector(v, 2), NORM_OK);
    double mag = sqrt((double)v[0]*v[0] + (double)v[1]*v[1]);
    ASSERT_NEAR(mag, 1.0, 1e-6);
    ASSERT_NEAR(v[0], -0.6f, 1e-6);
    ASSERT_NEAR(v[1], -0.8f, 1e-6);
}

static void test_norm_high_dimension() {
    // 64-dim vector, all 1s → normalize → each component = 1/sqrt(64) = 0.125
    const int D = 64;
    float v[D];
    for (int i = 0; i < D; i++) v[i] = 1.0f;
    ASSERT_EQ(normalize_vector(v, D), NORM_OK);
    double mag = 0;
    for (int i = 0; i < D; i++) mag += (double)v[i]*v[i];
    ASSERT_NEAR(sqrt(mag), 1.0, 1e-5);
    ASSERT_NEAR(v[0], 1.0/sqrt(64.0), 1e-6);
}

static void test_norm_single_component() {
    float v[] = {5.0f};
    ASSERT_EQ(normalize_vector(v, 1), NORM_OK);
    ASSERT_NEAR(v[0], 1.0f, 1e-6);
}

static void test_norm_std_vector() {
    // normalize_vec works on std::vector<float>
    vector<float> v = {0.0f, 0.0f, 3.0f};
    ASSERT_EQ(normalize_vec(v), NORM_OK);
    ASSERT_NEAR(v[2], 1.0f, 1e-6);
}

static void test_norm_idempotent() {
    // Normalizing twice gives the same result as normalizing once
    float v1[] = {2.0f, 3.0f, 6.0f};
    float v2[] = {2.0f, 3.0f, 6.0f};
    normalize_vector(v1, 3);
    normalize_vector(v2, 3);
    normalize_vector(v2, 3);  // second time
    ASSERT_NEAR(v1[0], v2[0], 1e-6);
    ASSERT_NEAR(v1[1], v2[1], 1e-6);
    ASSERT_NEAR(v1[2], v2[2], 1e-6);
}

// ═══════════════════════════════════════════════════════
// 2. COSINE DISTANCE TESTS
// ═══════════════════════════════════════════════════════

static void test_cosine_identical_vectors_zero() {
    // Same vector → cosine distance = 0
    float a[] = {1.0f, 2.0f, 3.0f};
    ASSERT_NEAR(vdb_dist_cosine(a, a, 3), 0.0, 1e-9);
}

static void test_cosine_opposite_vectors_max() {
    // Opposite vectors → cosine distance = 2
    float a[] = {1.0f, 0.0f};
    float b[] = {-1.0f, 0.0f};
    ASSERT_NEAR(vdb_dist_cosine(a, b, 2), 2.0, 1e-9);
}

static void test_cosine_orthogonal_vectors_one() {
    // Perpendicular vectors → cosine distance = 1
    float a[] = {1.0f, 0.0f};
    float b[] = {0.0f, 1.0f};
    ASSERT_NEAR(vdb_dist_cosine(a, b, 2), 1.0, 1e-9);
}

static void test_cosine_is_symmetric() {
    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {4.0f, 5.0f, 6.0f};
    ASSERT_NEAR(vdb_dist_cosine(a, b, 3), vdb_dist_cosine(b, a, 3), 1e-9);
}

static void test_cosine_range_0_to_2() {
    // cosine distance must always be in [0, 2]
    float a[] = { 3.0f, -1.0f, 2.0f};
    float b[] = {-1.0f,  4.0f, 0.5f};
    double d = vdb_dist_cosine(a, b, 3);
    ASSERT(d >= 0.0 && d <= 2.0);
}

static void test_cosine_zero_vector_returns_one() {
    // Zero vector → treated as maximally distant (returns 1.0)
    float a[] = {0.0f, 0.0f};
    float b[] = {1.0f, 0.0f};
    double d = vdb_dist_cosine(a, b, 2);
    ASSERT_NEAR(d, 1.0, 1e-9);
}

static void test_cosine_known_value() {
    // (1,0) vs (1,1)/sqrt(2) → cos = 1/sqrt(2) → dist = 1 - 1/sqrt(2) ≈ 0.2929
    float a[] = {1.0f, 0.0f};
    float b[] = {1.0f, 1.0f};
    double expected = 1.0 - 1.0/sqrt(2.0);
    ASSERT_NEAR(vdb_dist_cosine(a, b, 2), expected, 1e-6);
}

// ═══════════════════════════════════════════════════════
// 3. EUCLIDEAN vs COSINE COMPARISON TESTS
// ═══════════════════════════════════════════════════════

static void test_euclidean_and_cosine_agree_on_identical() {
    float a[] = {1.0f, 2.0f, 3.0f};
    // both must return 0 for identical vectors
    ASSERT_NEAR(vdb_dist_sq(a, a, 3),     0.0, 1e-9);
    ASSERT_NEAR(vdb_dist_cosine(a, a, 3), 0.0, 1e-9);
}

static void test_vdb_dist_dispatcher_euclidean() {
    float a[] = {3.0f, 0.0f};
    float b[] = {0.0f, 4.0f};
    // dist_sq = 9 + 16 = 25
    ASSERT_NEAR(vdb_dist(a, b, 2, metric_t::EUCLIDEAN), 25.0, 1e-6);
}

static void test_vdb_dist_dispatcher_cosine() {
    float a[] = {1.0f, 0.0f};
    float b[] = {0.0f, 1.0f};
    // orthogonal → cosine dist = 1
    ASSERT_NEAR(vdb_dist(a, b, 2, metric_t::COSINE), 1.0, 1e-9);
}

static void test_different_magnitude_same_direction() {
    // (1,0) and (100,0) point the same direction
    // cosine distance → 0 (same direction)
    // euclidean distance → big (9801 ≠ 0)
    float a[] = {1.0f,   0.0f};
    float b[] = {100.0f, 0.0f};
    ASSERT_NEAR(vdb_dist_cosine(a, b, 2), 0.0, 1e-9);
    ASSERT(vdb_dist_sq(a, b, 2) > 1000.0);   // euclidean sees a big difference
}

// ═══════════════════════════════════════════════════════
// 4. SEARCH WITH NORMALIZATION
// ═══════════════════════════════════════════════════════

static void test_search_brute_cosine_finds_same_direction() {
    // Insert 3 vectors. Query in direction (1,0).
    // (2,0) is same direction (should be nearest under cosine).
    // (0,1) is orthogonal. (-1,0) is opposite.
    vector_store_t vs{};
    vs_init(&vs, 2);

    float v1[] = { 2.0f,  0.0f};   // same direction as query
    float v2[] = { 0.0f,  1.0f};   // orthogonal
    float v3[] = {-1.0f,  0.0f};   // opposite

    // normalize before inserting (what Member 2 does on ADD)
    normalize_vector(v1, 2);
    normalize_vector(v2, 2);
    normalize_vector(v3, 2);

    vs_add(&vs, 1, v1);
    vs_add(&vs, 2, v2);
    vs_add(&vs, 3, v3);

    // query: direction (1,0)
    vector<float> query = {1.0f, 0.0f};
    normalize_vec(query);

    vector<search_result_t> results(3);
    int out_count = 0;
    search_brute(vs, query, 3, results, out_count, metric_t::COSINE);

    ASSERT_EQ(out_count, 3);
    // nearest must be id=1 (same direction, dist≈0)
    ASSERT_EQ(results[0].id, (int64_t)1);
    ASSERT_NEAR(results[0].distance, 0.0, 1e-6);
    // farthest must be id=3 (opposite, dist≈2)
    ASSERT_EQ(results[2].id, (int64_t)3);
    ASSERT_NEAR(results[2].distance, 2.0, 1e-6);

    vs_destroy(&vs);
}

static void test_search_euclidean_finds_closest_point() {
    // Under Euclidean, (1.1, 0) is closer to (1,0) than (0,1) is
    vector_store_t vs{};
    vs_init(&vs, 2);
    float v1[] = {1.1f, 0.0f};
    float v2[] = {0.0f, 1.0f};
    vs_add(&vs, 1, v1);
    vs_add(&vs, 2, v2);

    vector<float> query = {1.0f, 0.0f};
    vector<search_result_t> results(2);
    int out_count = 0;
    search_brute(vs, query, 2, results, out_count, metric_t::EUCLIDEAN);

    ASSERT_EQ(results[0].id, (int64_t)1);  // (1.1,0) is nearest
    vs_destroy(&vs);
}

// ═══════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════
int main() {
    cout << "\n=== Member 2 — Normalization + Cosine Test Suite ===\n";

    cout << "\n[ Normalization ]\n";
    run_test("unit_length_after_normalize",  test_norm_unit_length_after_normalize);
    run_test("values_correct_3_4_5",         test_norm_values_correct);
    run_test("already_unit_unchanged",       test_norm_already_unit);
    run_test("zero_vector_rejected",         test_norm_zero_vector_rejected);
    run_test("negative_components",          test_norm_negative_components);
    run_test("high_dimension_64d",           test_norm_high_dimension);
    run_test("single_component",             test_norm_single_component);
    run_test("std_vector_overload",          test_norm_std_vector);
    run_test("idempotent_normalize_twice",   test_norm_idempotent);

    cout << "\n[ Cosine distance ]\n";
    run_test("identical_vectors_zero",       test_cosine_identical_vectors_zero);
    run_test("opposite_vectors_two",         test_cosine_opposite_vectors_max);
    run_test("orthogonal_vectors_one",       test_cosine_orthogonal_vectors_one);
    run_test("is_symmetric",                 test_cosine_is_symmetric);
    run_test("range_0_to_2",                 test_cosine_range_0_to_2);
    run_test("zero_vector_returns_one",      test_cosine_zero_vector_returns_one);
    run_test("known_value_45_degrees",       test_cosine_known_value);

    cout << "\n[ Euclidean vs Cosine comparison ]\n";
    run_test("both_zero_on_identical",       test_euclidean_and_cosine_agree_on_identical);
    run_test("dispatcher_euclidean",         test_vdb_dist_dispatcher_euclidean);
    run_test("dispatcher_cosine",            test_vdb_dist_dispatcher_cosine);
    run_test("same_direction_diff_magnitude",test_different_magnitude_same_direction);

    cout << "\n[ Search with normalization ]\n";
    run_test("cosine_finds_same_direction",  test_search_brute_cosine_finds_same_direction);
    run_test("euclidean_finds_closest_point",test_search_euclidean_finds_closest_point);

    cout << "\n=====================================================\n";
    cout << "Results: " << tests_passed << " passed, "
         << tests_failed << " failed, "
         << tests_run    << " total\n\n";

    return tests_failed == 0 ? 0 : 1;
}