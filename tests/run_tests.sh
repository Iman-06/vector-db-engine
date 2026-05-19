#!/bin/bash
echo "========================================"
echo " VDB Test Suite"
echo " Start fresh server before running:"
echo " ./vdb --data ./vdata --dim 4 --port 5556"
echo "========================================"
echo ""
echo "  Running Protocol Tests  "
bash tests/test_protocol.sh
echo ""
echo "  Running Distance Tests  "
echo "NOTE: Restart server fresh before this"
bash tests/test_distance.sh
echo ""
echo "  Running k-Means Tests  "
echo "NOTE: Restart server fresh before this"
bash tests/test_kmeans.sh
echo ""
echo "  Running Snapshot Test  "
echo "NOTE: Run separately after Member 1 confirms SAVE/LOAD is done:"
echo " bash tests/test_snapshot.sh"
echo "========================================"