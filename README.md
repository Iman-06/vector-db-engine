# Vector Database Engine

A TCP based vector database server that stores fixed dimension float vectors and answers nearest neighbor queries.

## How to Build

* Requires g++ and pthreads on Ubuntu LTS
* Single command: make
* Produces two binaries: vdb (server) and vdb-cli (client)

## How to Run

Starting the server:
./vdb --data ./vdata --dim 4 --port 5556
(Note: --dim and --port can be changed, and --data is where the snapshot file is saved)

Starting the client (in a separate terminal):
./vdb-cli localhost 5556

## Supported Commands

ADD id v1 v2 ... vD
* Inserts a vector with the given id and D float components
* If id already exists, overwrites it
* Returns: OK

SEARCH v1 v2 ... vD k BRUTE
* Returns k nearest vectors using brute force scan
* Returns results sorted by distance with a summary line showing vectors scanned

SEARCH v1 v2 ... vD k IVF nprobe
* Returns k nearest vectors using the IVF index
* nprobe controls how many clusters to visit
* IVF index must be built first with BUILD
* Returns results sorted by distance with scanned count

BUILD
* Runs kmeans clustering on all stored vectors
* Number of clusters is floor(sqrt(N)) where N is total vectors
* After BUILD, new ADD commands are inserted into the nearest existing cluster
* Returns cluster count, iterations, and time taken

SAVE
* Saves all vectors and IVF index (if built) to ./vdata/snapshot.vdb
* Returns: OK saved N vectors

LOAD
* Loads vectors and IVF index from ./vdata/snapshot.vdb into memory
* Returns: OK loaded N vectors

STATS
* Prints dimension, total vector count, whether index is built, cluster count, and per-cluster sizes

QUIT
* Disconnects the client, server keeps running

## Known Limitations

* LOAD must be called manually after server restart, no auto load on startup
* BUILD blocks other clients while running
* Only Euclidean distance is supported
* No concurrent BUILD and ADD support
