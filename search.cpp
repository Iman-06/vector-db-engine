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