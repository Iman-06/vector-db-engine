 /*
 vdb_interface.h — Shared interface for the Vector DB Engine.
 Every cross-member function call goes through here so there is exactly one place to look up names, parameter order, and return codes.
 */

#ifndef VDB_INTERFACE_H
#define VDB_INTERFACE_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "vector_store.h"
using namespace std;
// Extra error codes
#define VDB_ERR_BADCMD   -10   /* unrecognised or malformed command  */
#define VDB_ERR_BADARGS  -11   /* wrong number / type of arguments   */
#define VDB_ERR_SEARCH   -12   /* search function reported a failure */

enum class search_mode_t {
    SEARCH_MODE_BRUTE = 0,
    SEARCH_MODE_ANN   = 1
};

/* ──────────────────────────────────────────────────────────────────────
 * search_result_t — one entry returned by any search function.
 *
 * store_index: the slot number inside vs->vectors / vs->ids where this
 *   vector lives. Member 3 MUST fill this field so Member 1 can retrieve
 *   the original float components for the response line without doing a
 *   slow ID scan.
 * ────────────────────────────────────────────────────────────────────── */
struct search_result_t {
    int64_t id = 0;           
    double distance = 0.0;    
    size_t store_index = 0;
};

// server_config_t — runtime settings parsed from command-line arguments.
struct server_config_t {
    int dim = 0;                 
    int port = 0;                
    string data_path;       
    vector_store_t* store = nullptr;
};
int search_brute(const vector_store_t& vs,const vector<float>& query,int k,vector<search_result_t>& out_results,int& out_count);
#endif