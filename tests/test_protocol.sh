#!/bin/bash
# Tests that the server correctly rejects malformed commands

SERVER="localhost"
PORT="5556"
PASS=0
FAIL=0

send_cmd() {
    echo -e "$1" | nc -q1 $SERVER $PORT
}

echo "Protocol Parser Tests"

# Test 1: ADD with wrong number of components (dim=4, sending 3)
OUT=$(send_cmd "ADD 1 0.1 0.2 0.3")
if echo "$OUT" | grep -qi "err"; then
    echo "PASS: ADD with too few components rejected"
    PASS=$((PASS+1)) 
else
    echo "FAIL: ADD with too few components should return error"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Test 2: ADD with too many components
OUT=$(send_cmd "ADD 2 0.1 0.2 0.3 0.4 0.5")
if echo "$OUT" | grep -qi "err"; then
    echo "PASS: ADD with too many components rejected"
    PASS=$((PASS+1))
else
    echo "FAIL: ADD with too many components should return error"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Test 3: SEARCH IVF before BUILD
OUT=$(send_cmd "SEARCH 0.1 0.2 0.3 0.4 2 IVF 1")
if echo "$OUT" | grep -qi "err"; then
    echo "PASS: IVF search before BUILD rejected"
    PASS=$((PASS+1))
else
    echo "FAIL: IVF search before BUILD should return error"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Test 4: Unknown command
OUT=$(send_cmd "HELLO")
if echo "$OUT" | grep -qi "err\|unknown"; then
    echo "PASS: Unknown command rejected"
    PASS=$((PASS+1))
else
    echo "FAIL: Unknown command should return error"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

# Test 5: Valid ADD returns OK
OUT=$(send_cmd "ADD 99 0.1 0.2 0.3 0.4")
if echo "$OUT" | grep -qi "ok"; then
    echo "PASS: Valid ADD returns OK"
    PASS=$((PASS+1))
else
    echo "FAIL: Valid ADD should return OK"
    echo "      Got: $OUT"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Protocol Tests: $PASS passed, $FAIL failed"
exit $FAIL