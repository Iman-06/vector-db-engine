#!/bin/bash
# Inserts 8 vectors that clearly form 2 clusters, verifies BUILD works
#
# Cluster A: near (0, 0, 0, 0)  -- IDs 1-4
# Cluster B: near (10, 10, 10, 10) -- IDs 5-8
# sqrt(8) = 2, so K=2 clusters expected

SERVER="localhost"
PORT="5556"
PASS=0
FAIL=0

send_cmd() {
    echo -e "$1" | nc -q1 $SERVER $PORT
}

echo "k-Means / BUILD Tests"

# Insert cluster A
send_cmd "ADD 1 0.1 0.1 0.1 0.1" > /dev/null
send_cmd "ADD 2 0.2 0.1 0.1 0.2" > /dev/null
send_cmd "ADD 3 0.1 0.2 0.2 0.1" > /dev/null
send_cmd "ADD 4 0.2 0.2 0.1 0.1" > /dev/null

# Insert cluster B
send_cmd "ADD 5 9.9 10.1 9.8 10.2" > /dev/null
send_cmd "ADD 6 10.1 9.9 10.2 9.8" > /dev/null
send_cmd "ADD 7 10.0 10.0 9.9 10.1" > /dev/null
send_cmd "ADD 8 9.8 10.2 10.1 9.9" > /dev/null

# Run BUILD
OUT=$(send_cmd "BUILD")
if echo "$OUT" | grep -qi "ok\|done"; then
    echo "PASS: BUILD completed successfully"
    PASS=$((PASS+1))
else
    echo "FAIL: BUILD did not return OK/done"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Check STATS shows index built and 2 clusters
STATS=$(send_cmd "STATS")

if echo "$STATS" | grep -qi "index built.*yes\|yes"; then  #the *yes is for "index built: yes" or just "yes" in a summary line
    echo "PASS: STATS shows index is built"
    PASS=$((PASS+1))
else
    echo "FAIL: STATS does not show index as built"
    echo "      Got: $STATS"
    FAIL=$((FAIL+1))
fi

if echo "$STATS" | grep -qi "clusters.*2\|2.*cluster"; then #the *2 is for "clusters: 2" or just "2" in a summary line
    echo "PASS: STATS shows 2 clusters"
    PASS=$((PASS+1))
else
    echo "FAIL: Expected 2 clusters in STATS"
    echo "      Got: $STATS"
    FAIL=$((FAIL+1))
fi

# IVF search with nprobe=2 (visits all clusters) should match BRUTE
BRUTE=$(send_cmd "SEARCH 0.1 0.1 0.1 0.1 2 BRUTE")
IVF=$(send_cmd "SEARCH 0.1 0.1 0.1 0.1 2 IVF 2")
echo "DEBUG BRUTE: $BRUTE"
echo "DEBUG IVF: $IVF"
BRUTE_IDS=$(echo "$BRUTE" | grep -v "^(" | awk '{print $1}' | sort) # Extract IDs ignore lines starting with '(', sort for comparison
IVF_IDS=$(echo "$IVF" | grep -v "^(" | awk '{print $1}' | sort)

if [ "$BRUTE_IDS" = "$IVF_IDS" ]; then
    echo "PASS: IVF with full nprobe matches BRUTE results"
    PASS=$((PASS+1))
else
    echo "FAIL: IVF results differ from BRUTE at full nprobe"
    echo "      BRUTE IDs: $BRUTE_IDS"
    echo "      IVF IDs:   $IVF_IDS"
    FAIL=$((FAIL+1))
fi

echo ""
echo "k-Means Tests: $PASS passed, $FAIL failed"
exit $FAIL