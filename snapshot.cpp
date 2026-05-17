#include "snapshot.h"
#include "vector_store.h"
#include "ivf.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Binary file format:
//   [4 bytes] magic:   "VDB1"
//   [4 bytes] version: uint32_t = 1
//   [4 bytes] dim:     int32_t
//   [8 bytes] count:   uint64_t
//   then for each vector:
//     [8 bytes] id:    int64_t
//     [dim * 4 bytes] floats
//   [1 byte]  has_ivf: 0 or 1
//   if has_ivf:
//     [4 bytes] k:     int32_t
//     [k * dim * 4 bytes] centroids (floats)
//     for each cluster c in 0..k:
//       [4 bytes] cluster_size: int32_t
//       [cluster_size * 4 bytes] indices (int32_t)

static const char MAGIC[4] = {'V','D','B','1'};
static const uint32_t VERSION = 1;

// ---------- helpers ----------
static bool write_all(FILE* f, const void* buf, size_t n) {
    return fwrite(buf, 1, n, f) == n;
}
static bool read_all(FILE* f, void* buf, size_t n) {
    return fread(buf, 1, n, f) == n;
}

// ---------- SAVE ----------
int snapshot_save(const vector_store_t* vs, const ivf_index_t* ivf, const std::string& path) {
    // Write to a temp file first, then rename (atomic swap)
    std::string tmp = path + ".tmp";

    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return SNAP_ERR_IO;

    // Header
    uint32_t ver = VERSION;
    int32_t  dim = (int32_t)vs->dim;
    uint64_t cnt = (uint64_t)vs->count;

    if (!write_all(f, MAGIC, 4))             goto fail;
    if (!write_all(f, &ver,  sizeof(ver)))   goto fail;
    if (!write_all(f, &dim,  sizeof(dim)))   goto fail;
    if (!write_all(f, &cnt,  sizeof(cnt)))   goto fail;

    // Vectors
    for (uint64_t i = 0; i < cnt; i++) {
        int64_t id = vs->ids[i];
        const float* vec = vs->vectors + i * (size_t)vs->dim;
        if (!write_all(f, &id,  sizeof(id)))                        goto fail;
        if (!write_all(f, vec, (size_t)vs->dim * sizeof(float)))    goto fail;
    }

    // IVF index (optional)
    {
        uint8_t has_ivf = (ivf && ivf->built) ? 1 : 0;
        if (!write_all(f, &has_ivf, 1)) goto fail;

        if (has_ivf) {
            int32_t k = (int32_t)ivf->k;
            if (!write_all(f, &k, sizeof(k))) goto fail;

            // centroids: k * dim floats
            if (!write_all(f, ivf->centroids.data(),
                           (size_t)k * (size_t)vs->dim * sizeof(float))) goto fail;

            // clusters
            for (int c = 0; c < k; c++) {
                int32_t sz = (int32_t)ivf->clusters[c].size();
                if (!write_all(f, &sz, sizeof(sz))) goto fail;
                if (sz > 0) {
                    if (!write_all(f, ivf->clusters[c].data(),
                                   (size_t)sz * sizeof(int32_t))) goto fail;
                }
            }
        }
    }

    fclose(f);

    // Atomic rename: tmp -> final
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return SNAP_ERR_IO;
    }
    return SNAP_OK;

fail:
    fclose(f);
    remove(tmp.c_str());
    return SNAP_ERR_IO;
}

// ---------- LOAD ----------
int snapshot_load(vector_store_t* vs, ivf_index_t* ivf, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return SNAP_ERR_IO;   // file doesn't exist yet — that's fine on first run

    // Check magic
    char magic[4];
    if (!read_all(f, magic, 4) || memcmp(magic, MAGIC, 4) != 0) {
        fclose(f); return SNAP_ERR_MAGIC;
    }

    uint32_t ver;
    if (!read_all(f, &ver, sizeof(ver)) || ver != VERSION) {
        fclose(f); return SNAP_ERR_VER;
    }

    int32_t  dim;
    uint64_t cnt;
    if (!read_all(f, &dim, sizeof(dim))) { fclose(f); return SNAP_ERR_IO; }
    if (!read_all(f, &cnt, sizeof(cnt))) { fclose(f); return SNAP_ERR_IO; }

    // Re-init the store with the saved dimension
    vs_destroy(vs);
    if (vs_init(vs, (int)dim) != VS_OK) { fclose(f); return SNAP_ERR_NOMEM; }

    // Read vectors
    std::vector<float> buf((size_t)dim);
    for (uint64_t i = 0; i < cnt; i++) {
        int64_t id;
        if (!read_all(f, &id, sizeof(id)))                            goto fail;
        if (!read_all(f, buf.data(), (size_t)dim * sizeof(float)))    goto fail;
        if (vs_add(vs, id, buf.data()) != VS_OK)                      goto fail;
    }

    // IVF
    {
        uint8_t has_ivf = 0;
        if (!read_all(f, &has_ivf, 1)) goto fail;

        if (has_ivf) {
            int32_t k;
            if (!read_all(f, &k, sizeof(k))) goto fail;

            ivf->k    = (int)k;
            ivf->dim  = (int)dim;
            ivf->built = false;  // set true only after full load

            ivf->centroids.resize((size_t)k * (size_t)dim);
            if (!read_all(f, ivf->centroids.data(),
                          (size_t)k * (size_t)dim * sizeof(float))) goto fail;

            ivf->clusters.resize((size_t)k);
            for (int c = 0; c < k; c++) {
                int32_t sz;
                if (!read_all(f, &sz, sizeof(sz))) goto fail;
                ivf->clusters[c].resize((size_t)sz);
                if (sz > 0) {
                    if (!read_all(f, ivf->clusters[c].data(),
                                  (size_t)sz * sizeof(int32_t))) goto fail;
                }
            }
            ivf->built = true;
        }
    }

    fclose(f);
    return SNAP_OK;

fail:
    fclose(f);
    return SNAP_ERR_IO;
}