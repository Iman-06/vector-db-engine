#!/bin/bash
# Verifies distance ordering is correct using hand-computed values
#
# Vectors (dim=4):
#   ID 10: (1.0, 0.0, 0.0, 0.0)
#   ID 20: (0.0, 1.0, 0.0, 0.0)
#
# Query: (1.0, 0.0, 0.0, 0.0)
# dist^2 to ID 10 = 0.0  (exact match)
# dist^2 to ID 20 = 2.0  (1^2 + 1^2)
# So ID 10 must be closer

SERVER="localhost"
PORT="5556"
PASS=0
FAIL=0

send_cmd() {
    echo -e "$1" | nc -q1 $SERVER $PORT
}

echo "Distance Function Tests"

send_cmd "ADD 10 1.0 0.0 0.0 0.0" > /dev/null # /dev/null discards output we only care about SEARCH results
send_cmd "ADD 20 0.0 1.0 0.0 0.0" > /dev/null

# Search for nearest to (1.0 0.0 0.0 0.0), k=1
OUT=$(send_cmd "SEARCH 1.0 0.0 0.0 0.0 1 BRUTE")

if echo "$OUT" | grep -q "^10 "; then
    echo "PASS: Closest vector to (1,0,0,0) is correctly ID 10"
    PASS=$((PASS+1))
else
    echo "FAIL: Expected ID 10 as closest"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Search k=2, verify order: ID 10 first, ID 20 second
LINE1=$(echo "$OUT" | grep -v "^(" | sed -n '1p' | awk '{print $1}') # this prints the first line's first word
LINE2=$(echo "$OUT" | grep -v "^(" | sed -n '2p' | awk '{print $1}')

if [ "$LINE1" = "10" ]; then
    echo "PASS: Distance ordering correct, ID 10 is closest"
    PASS=$((PASS+1))
else
    echo "FAIL: Wrong ordering. ID 10 should be first, got: $LINE1"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Distance Tests: $PASS passed, $FAIL failed"
exit $FAIL