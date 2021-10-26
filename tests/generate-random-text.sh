#!/bin/bash

# Generate one randomized input without control characters or newlines.

set -e

cat /dev/urandom | sed 's/[^\d32-\d126]//g' | tr -d "\n" | head -c 200000 > random-200000B.txt
