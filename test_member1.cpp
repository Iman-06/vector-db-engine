// tests/test_member1.cpp
// Member 1 — Persistence tests
// Tests: distance function, parser logic, k-means sanity, SAVE/LOAD round-trip
//
// Build:
//   g++ -std=c++17 -pthread -Wall -g \
//       tests/test_member1.cpp vector_store.cpp snapshot.cpp ivf.cpp kmeans.cpp search.cpp \
//       -o tests/test_member1
// Run:
//   ./tests/test_member1

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "vector_store.h"
#include "snapshot.h"
#include "ivf.h"
#include "vdb_interface.h"

using namespace std;

// ─── tiny test framework ────────────────────────────────────────────────────
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    void name(); \
    struct _reg_##name { _reg_##name() { run_test(#name, name); } } _inst_##name; \
    void name()

static void run_test(const char* name, void (*fn)()) {
    tests_run++;
    try {
        fn();
        tests_passed++;
        cout << "  [PASS] " << name << "\n";
    } catch (const exception& e) {
        tests_failed++;
        cout << "  [FAIL] " << name << " — " << e.what() << "\n";
    } catch (...) {
        tests_failed++;
        cout << "  [FAIL] " << name << " — unknown exception\n";
    }
}

#define ASSERT(cond) \
    do { if (!(cond)) throw runtime_error("assertion failed: " #cond " (line " + to_string(__LINE__) + ")"); } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw runtime_error(string("expected ") + to_string(b) + " got " + to_string(a) + " (line " + to_string(__LINE__) + ")"); } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { if (fabs((double)(a) - (double)(b)) > (eps)) throw runtime_error(string("values not close: ") + to_string(a) + " vs " + to_string(b) + " (line " + to_string(__LINE__) + ")"); } while(0)

// ─── helper: remove file if exists ──────────────────────────────────────────
static void rm(const string& path) { remove(path.c_str()); }

// ════════════════════════════════════════════════════════════════════════════
// 1. DISTANCE FUNCTION TESTS
// ════════════════════════════════════════════════════════════════════════════

TEST(distance_zero_same_vector) {
    // distance from a vector to itself must be 0
    float a[] = {1.0f, 2.0f, 3.0f, 4.0f};
    double d = vdb_dist_sq(a, a, 4);
    ASSERT_NEAR(d, 0.0, 1e-9);
}

TEST(distance_known_value) {
    // (3,0) vs (0,4) → dist_sq = 9+16 = 25
    float a[] = {3.0f, 0.0f};
    float b[] = {0.0f, 4.0f};
    double d = vdb_dist_sq(a, b, 2);
    ASSERT_NEAR(d, 25.0, 1e-6);
}

TEST(distance_is_symmetric) {
    float a[] = {1.0f, 5.0f, -2.0f};
    float b[] = {4.0f, 1.0f,  3.0f};
    double d1 = vdb_dist_sq(a, b, 3);
    double d2 = vdb_dist_sq(b, a, 3);
    ASSERT_NEAR(d1, d2, 1e-9);
}

TEST(distance_single_dimension) {
    float a[] = {7.0f};
    float b[] = {3.0f};
    double d = vdb_dist_sq(a, b, 1);
    ASSERT_NEAR(d, 16.0, 1e-9);
}

TEST(distance_high_dimension) {
    // 64-dim zero vector vs unit vector in dim 0 → dist_sq = 1
    float a[64] = {};
    float b[64] = {};
    b[0] = 1.0f;
    double d = vdb_dist_sq(a, b, 64);
    ASSERT_NEAR(d, 1.0, 1e-6);
}

// ════════════════════════════════════════════════════════════════════════════
// 2. VECTOR STORE (parser-layer) TESTS
// ════════════════════════════════════════════════════════════════════════════

TEST(vs_init_and_destroy) {
    vector_store_t vs{};
    ASSERT_EQ(vs_init(&vs, 4), VS_OK);
    ASSERT_EQ(vs.dim, 4);
    ASSERT_EQ(vs_count(&vs), (size_t)0);
    vs_destroy(&vs);
}

TEST(vs_add_single_vector) {
    vector_store_t vs{};
    vs_init(&vs, 3);
    float v[] = {1.0f, 2.0f, 3.0f};
    ASSERT_EQ(vs_add(&vs, 42, v), VS_OK);
    ASSERT_EQ(vs_count(&vs), (size_t)1);
    ASSERT_EQ(vs_get_id(&vs, 0), (int64_t)42);
    const float* got = vs_get_vector(&vs, 0);
    ASSERT_NEAR(got[0], 1.0f, 1e-6);
    ASSERT_NEAR(got[1], 2.0f, 1e-6);
    ASSERT_NEAR(got[2], 3.0f, 1e-6);
    vs_destroy(&vs);
}

TEST(vs_add_multiple_vectors) {
    vector_store_t vs{};
    vs_init(&vs, 2);
    float v1[] = {1.0f, 0.0f};
    float v2[] = {0.0f, 1.0f};
    float v3[] = {3.0f, 4.0f};
    vs_add(&vs, 1, v1);
    vs_add(&vs, 2, v2);
    vs_add(&vs, 3, v3);
    ASSERT_EQ(vs_count(&vs), (size_t)3);
    ASSERT_EQ(vs_get_id(&vs, 2), (int64_t)3);
    vs_destroy(&vs);
}

TEST(vs_add_duplicate_id_overwrites) {
    vector_store_t vs{};
    vs_init(&vs, 2);
    float v1[] = {1.0f, 1.0f};
    float v2[] = {9.0f, 9.0f};
    vs_add(&vs, 99, v1);
    vs_add(&vs, 99, v2);  // same id — should overwrite, not grow
    ASSERT_EQ(vs_count(&vs), (size_t)1);
    const float* got = vs_get_vector(&vs, 0);
    ASSERT_NEAR(got[0], 9.0f, 1e-6);
    vs_destroy(&vs);
}

TEST(vs_get_vector_out_of_bounds_returns_null) {
    vector_store_t vs{};
    vs_init(&vs, 2);
    const float* p = vs_get_vector(&vs, 999);
    ASSERT(p == nullptr);
    vs_destroy(&vs);
}

// ════════════════════════════════════════════════════════════════════════════
// 3. K-MEANS SANITY TESTS
// ════════════════════════════════════════════════════════════════════════════

TEST(kmeans_two_clear_clusters) {
    // Two well-separated groups — kmeans should split them perfectly
    // Group A: vectors near (0,0); Group B: vectors near (100,100)
    vector_store_t vs{};
    vs_init(&vs, 2);

    float a1[] = {0.1f, 0.2f};  float a2[] = {0.3f, 0.1f};  float a3[] = {0.2f, 0.3f};
    float b1[] = {100.1f, 100.2f}; float b2[] = {99.9f, 100.1f}; float b3[] = {100.3f, 99.8f};

    vs_add(&vs, 1, a1); vs_add(&vs, 2, a2); vs_add(&vs, 3, a3);
    vs_add(&vs, 4, b1); vs_add(&vs, 5, b2); vs_add(&vs, 6, b3);

    ivf_index_t ivf{};
    int rc = ivf_build(vs, ivf);
    ASSERT_EQ(rc, VS_OK);
    ASSERT(ivf.built);
    ASSERT(ivf.k >= 1);

    // Total assigned vectors must equal store count
    size_t total = 0;
    for (int c = 0; c < ivf.k; c++) total += ivf.clusters[c].size();
    ASSERT_EQ(total, vs_count(&vs));

    vs_destroy(&vs);
}

TEST(kmeans_single_vector) {
    vector_store_t vs{};
    vs_init(&vs, 3);
    float v[] = {1.0f, 2.0f, 3.0f};
    vs_add(&vs, 1, v);

    ivf_index_t ivf{};
    int rc = ivf_build(vs, ivf);
    ASSERT_EQ(rc, VS_OK);
    ASSERT(ivf.built);

    size_t total = 0;
    for (int c = 0; c < ivf.k; c++) total += ivf.clusters[c].size();
    ASSERT_EQ(total, (size_t)1);

    vs_destroy(&vs);
}

TEST(kmeans_all_vectors_assigned) {
    // 20 random-ish vectors — every one must end up in some cluster
    vector_store_t vs{};
    vs_init(&vs, 4);
    for (int i = 0; i < 20; i++) {
        float v[4] = {(float)i, (float)(i*2), (float)(i%3), 1.0f};
        vs_add(&vs, i, v);
    }
    ivf_index_t ivf{};
    ASSERT_EQ(ivf_build(vs, ivf), VS_OK);

    size_t total = 0;
    for (int c = 0; c < ivf.k; c++) total += ivf.clusters[c].size();
    ASSERT_EQ(total, vs_count(&vs));

    vs_destroy(&vs);
}

// ════════════════════════════════════════════════════════════════════════════
// 4. SNAPSHOT SAVE / LOAD ROUND-TRIP TESTS
// ════════════════════════════════════════════════════════════════════════════

static const string SNAP = "/tmp/test_vdb_snapshot.vdb";

TEST(snapshot_save_creates_file) {
    rm(SNAP);
    vector_store_t vs{};
    vs_init(&vs, 2);
    float v[] = {1.0f, 2.0f};
    vs_add(&vs, 7, v);

    ivf_index_t ivf{};
    int rc = snapshot_save(&vs, &ivf, SNAP);
    ASSERT_EQ(rc, SNAP_OK);

    // file must exist and be non-zero
    FILE* f = fopen(SNAP.c_str(), "rb");
    ASSERT(f != nullptr);
    fseek(f, 0, SEEK_END);
    ASSERT(ftell(f) > 0);
    fclose(f);

    vs_destroy(&vs);
    rm(SNAP);
}

TEST(snapshot_load_missing_file_returns_err_io) {
    rm(SNAP);
    vector_store_t vs{};
    vs_init(&vs, 2);
    ivf_index_t ivf{};
    int rc = snapshot_load(&vs, &ivf, SNAP);
    ASSERT_EQ(rc, SNAP_ERR_IO);
    vs_destroy(&vs);
}

TEST(snapshot_roundtrip_vectors_only) {
    rm(SNAP);

    // --- save ---
    vector_store_t vs_save{};
    vs_init(&vs_save, 3);
    float v1[] = {1.0f, 2.0f, 3.0f};
    float v2[] = {4.0f, 5.0f, 6.0f};
    float v3[] = {7.0f, 8.0f, 9.0f};
    vs_add(&vs_save, 10, v1);
    vs_add(&vs_save, 20, v2);
    vs_add(&vs_save, 30, v3);

    ivf_index_t ivf_save{};
    ASSERT_EQ(snapshot_save(&vs_save, &ivf_save, SNAP), SNAP_OK);
    vs_destroy(&vs_save);

    // --- load ---
    vector_store_t vs_load{};
    vs_init(&vs_load, 3);  // will be re-init'd by load
    ivf_index_t ivf_load{};
    ASSERT_EQ(snapshot_load(&vs_load, &ivf_load, SNAP), SNAP_OK);

    // count
    ASSERT_EQ(vs_count(&vs_load), (size_t)3);
    // dim preserved
    ASSERT_EQ(vs_load.dim, 3);
    // IVF should NOT be built (we saved without one)
    ASSERT(!ivf_load.built);

    // check every vector and id
    // find id 10
    bool found10 = false, found20 = false, found30 = false;
    for (size_t i = 0; i < vs_count(&vs_load); i++) {
        int64_t id = vs_get_id(&vs_load, i);
        const float* v = vs_get_vector(&vs_load, i);
        if (id == 10) {
            ASSERT_NEAR(v[0], 1.0f, 1e-6);
            ASSERT_NEAR(v[1], 2.0f, 1e-6);
            ASSERT_NEAR(v[2], 3.0f, 1e-6);
            found10 = true;
        } else if (id == 20) {
            ASSERT_NEAR(v[0], 4.0f, 1e-6);
            found20 = true;
        } else if (id == 30) {
            ASSERT_NEAR(v[2], 9.0f, 1e-6);
            found30 = true;
        }
    }
    ASSERT(found10); ASSERT(found20); ASSERT(found30);

    vs_destroy(&vs_load);
    rm(SNAP);
}

TEST(snapshot_roundtrip_with_ivf) {
    rm(SNAP);

    // --- build store + IVF then save ---
    vector_store_t vs_save{};
    vs_init(&vs_save, 2);
    for (int i = 0; i < 10; i++) {
        float v[2] = {(float)i, (float)(i * 2)};
        vs_add(&vs_save, i, v);
    }
    ivf_index_t ivf_save{};
    ASSERT_EQ(ivf_build(vs_save, ivf_save), VS_OK);
    ASSERT(ivf_save.built);
    int saved_k = ivf_save.k;

    ASSERT_EQ(snapshot_save(&vs_save, &ivf_save, SNAP), SNAP_OK);
    vs_destroy(&vs_save);

    // --- load ---
    vector_store_t vs_load{};
    vs_init(&vs_load, 2);
    ivf_index_t ivf_load{};
    ASSERT_EQ(snapshot_load(&vs_load, &ivf_load, SNAP), SNAP_OK);

    // vectors restored
    ASSERT_EQ(vs_count(&vs_load), (size_t)10);

    // IVF restored
    ASSERT(ivf_load.built);
    ASSERT_EQ(ivf_load.k, saved_k);
    ASSERT_EQ(ivf_load.dim, 2);

    // all cluster indices accounted for
    size_t total = 0;
    for (int c = 0; c < ivf_load.k; c++) total += ivf_load.clusters[c].size();
    ASSERT_EQ(total, (size_t)10);

    vs_destroy(&vs_load);
    rm(SNAP);
}

TEST(snapshot_corrupt_magic_rejected) {
    rm(SNAP);
    // write garbage
    FILE* f = fopen(SNAP.c_str(), "wb");
    ASSERT(f != nullptr);
    const char junk[] = "JUNK\x01\x00\x00\x00";
    fwrite(junk, 1, 8, f);
    fclose(f);

    vector_store_t vs{};
    vs_init(&vs, 2);
    ivf_index_t ivf{};
    int rc = snapshot_load(&vs, &ivf, SNAP);
    ASSERT_EQ(rc, SNAP_ERR_MAGIC);

    vs_destroy(&vs);
    rm(SNAP);
}

TEST(snapshot_tmp_file_removed_on_success) {
    rm(SNAP);
    string tmp = SNAP + ".tmp";
    rm(tmp);

    vector_store_t vs{};
    vs_init(&vs, 2);
    float v[] = {1.0f, 2.0f};
    vs_add(&vs, 1, v);
    ivf_index_t ivf{};

    ASSERT_EQ(snapshot_save(&vs, &ivf, SNAP), SNAP_OK);

    // tmp file must be gone after successful save
    FILE* f = fopen(tmp.c_str(), "rb");
    ASSERT(f == nullptr);   // should not exist

    vs_destroy(&vs);
    rm(SNAP);
}

TEST(snapshot_large_roundtrip) {
    rm(SNAP);
    const int N = 1000;
    const int D = 16;

    vector_store_t vs_save{};
    vs_init(&vs_save, D);

    for (int i = 0; i < N; i++) {
        float v[D];
        for (int d = 0; d < D; d++) v[d] = (float)(i * D + d) * 0.01f;
        vs_add(&vs_save, (int64_t)i, v);
    }
    ivf_index_t ivf_save{};
    ASSERT_EQ(snapshot_save(&vs_save, &ivf_save, SNAP), SNAP_OK);
    vs_destroy(&vs_save);

    vector_store_t vs_load{};
    vs_init(&vs_load, D);
    ivf_index_t ivf_load{};
    ASSERT_EQ(snapshot_load(&vs_load, &ivf_load, SNAP), SNAP_OK);
    ASSERT_EQ(vs_count(&vs_load), (size_t)N);
    ASSERT_EQ(vs_load.dim, D);

    // spot-check vector 500
    const float* v500 = vs_get_vector(&vs_load, 500);
    ASSERT(v500 != nullptr);
    ASSERT_NEAR(v500[0], 500 * D * 0.01f, 1e-4);

    vs_destroy(&vs_load);
    rm(SNAP);
}

// ════════════════════════════════════════════════════════════════════════════
// main
// ════════════════════════════════════════════════════════════════════════════
int main() {
    cout << "\n=== Member 1 — Persistence Test Suite ===\n\n";

    cout << "[ Distance function tests ]\n";
    // (tests registered and run via static constructors above)

    cout << "\n[ Vector store tests ]\n";
    cout << "\n[ K-means sanity tests ]\n";
    cout << "\n[ Snapshot SAVE/LOAD round-trip tests ]\n";

    cout << "\n==========================================\n";
    cout << "Results: " << tests_passed << " passed, "
         << tests_failed << " failed, "
         << tests_run    << " total\n";

    return tests_failed == 0 ? 0 : 1;
}