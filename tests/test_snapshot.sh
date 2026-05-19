#!/bin/bash
# test_snapshot.sh
# Round-trip test: ADD vectors, SAVE, restart server, LOAD, verify SEARCH still works

SERVER="localhost"
PORT="5556"
PASS=0
FAIL=0

send_cmd() {
    echo -e "$1" | nc -q1 $SERVER $PORT
}

echo "=== SAVE/LOAD Round-Trip Tests ==="

# Add known vectors
send_cmd "ADD 101 0.5 0.5 0.5 0.5" > /dev/null
send_cmd "ADD 102 0.9 0.1 0.1 0.1" > /dev/null
send_cmd "ADD 103 0.1 0.9 0.1 0.1" > /dev/null

# Save
OUT=$(send_cmd "SAVE")
if echo "$OUT" | grep -qi "saved\|ok"; then
    echo "PASS: SAVE returned success"
    PASS=$((PASS+1))
else
    echo "FAIL: SAVE did not return success"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

echo ""
echo "--- Restart the server now, then press ENTER ---"
read

# Send LOAD command manually
OUT=$(send_cmd "LOAD")
if echo "$OUT" | grep -qi "loaded\|ok"; then
    echo "PASS: LOAD returned success"
    PASS=$((PASS+1))
else
    echo "FAIL: LOAD did not return success"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Verify vector 101 is back
OUT=$(send_cmd "SEARCH 0.5 0.5 0.5 0.5 1 BRUTE")
if echo "$OUT" | grep -q "^101 "; then
    echo "PASS: Vector ID 101 survived SAVE/LOAD"
    PASS=$((PASS+1))
else
    echo "FAIL: Vector ID 101 not found after LOAD"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Verify all 3 vectors are back
STATS=$(send_cmd "STATS")
if echo "$STATS" | grep -qi "total vectors.*3\|3.*vector"; then
    echo "PASS: All 3 vectors restored after LOAD"
    PASS=$((PASS+1))
else
    echo "FAIL: Vector count wrong after LOAD"
    echo "      Got: $STATS"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Snapshot Tests: $PASS passed, $FAIL failed"
exit $FAIL