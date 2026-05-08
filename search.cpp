#include "vdb_interface.h"
#include "vector_store.h"
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
using namespace std;
static double distance_sq(const vector<float>& a, const float* b, int dim)
{ //using sqaure distance to avoid unnecessary sqrt computations
    double sum = 0;
    for (int i = 0; i < dim; i++)
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    return sum;
}
struct Compare {
    bool operator()(const search_result_t& x,const search_result_t& y) {
        return x.distance < y.distance;
    }
};
int search_brute(const vector_store_t& vs,const vector<float>& query,int k,vector<search_result_t>& out_results,int& out_count)
{
    out_results.clear();
    out_count = 0;
    int n = vs.count;
    if (n == 0)
        return VS_OK;

    if (k > n)
        k = n;
    priority_queue<search_result_t,vector<search_result_t>,Compare> maxHeap;
    for (int i = 0; i < n; i++)
    {
        const float* vec = vs_get_vector(&vs, i);
        double dist = distance_sq(query, vec, vs.dim);
        search_result_t temp;
        temp.id = vs_get_id(&vs, i);
        temp.distance = dist;
        temp.store_index = i;
        if ((int)maxHeap.size() < k)
        {
            maxHeap.push(temp);
        }
        else if (dist < maxHeap.top().distance)
        {
            maxHeap.pop();
            maxHeap.push(temp);
        }
    }
    while (!maxHeap.empty())
    {
        out_results.push_back(maxHeap.top());
        maxHeap.pop();
    }
    sort(out_results.begin(), out_results.end(),[](const search_result_t& a,const search_result_t& b)
        {
            return a.distance < b.distance;
        });
    out_count = out_results.size();
    return VS_OK;
}
// Add this after the existing search_brute function
int search_ivf(const vector_store_t& vs,
    const ivf_index_t& ivf,
    const vector<float>& query,
    int k,
    int nprobe,
    vector<search_result_t>& out_results,
    int& out_count,
    size_t& out_scanned)
{
    out_results.clear();
    out_count = 0;
    out_scanned = 0;

    // Check if IVF index is built
    if (!ivf.built || ivf.k == 0) {
        return VS_ERR_BADINDEX;
    }

    int n = vs.count;
    if (n == 0) return VS_OK;
    if (k > n) k = n;
    if (nprobe > ivf.k) nprobe = ivf.k;

    // Find nprobe nearest centroids to the query
    vector<pair<double, int>> centroid_distances; // (distance, centroid_index)

    for (int c = 0; c < ivf.k; c++) {
        const float* centroid = ivf.centroids.data() + (size_t)c * (size_t)ivf.dim;
        double dist = vdb_dist_sq(query.data(), centroid, ivf.dim);
        centroid_distances.push_back({ dist, c });
    }

    // Sort centroids by distance (nearest first)
    sort(centroid_distances.begin(), centroid_distances.end());

    //Collect vectors from the nprobe closest clusters
    priority_queue<search_result_t, vector<search_result_t>, Compare> maxHeap;
    for (int probe = 0; probe < nprobe; probe++) {
        int cluster_idx = centroid_distances[probe].second;
        const vector<int>& cluster = ivf.clusters[cluster_idx];
        out_scanned += cluster.size();
        // Search all vectors in this cluster
        for (int store_idx : cluster) {
            const float* vec = vs_get_vector(&vs, (size_t)store_idx);
            double dist = vdb_dist_sq(query.data(), vec, ivf.dim);

            search_result_t temp;
            temp.id = vs_get_id(&vs, (size_t)store_idx);
            temp.distance = dist;
            temp.store_index = (size_t)store_idx;

            if ((int)maxHeap.size() < k) {
                maxHeap.push(temp);
            }
            else if (dist < maxHeap.top().distance) {
                maxHeap.pop();
                maxHeap.push(temp);
            }
        }
    }

    // Step 3: Extract results from heap and sort
    while (!maxHeap.empty()) {
        out_results.push_back(maxHeap.top());
        maxHeap.pop();
    }
    sort(out_results.begin(), out_results.end(),
        [](const search_result_t& a, const search_result_t& b) {
            return a.distance < b.distance;
        });

    out_count = (int)out_results.size();
    return VS_OK;
}