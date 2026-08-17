#!/usr/bin/bash

# Run multiple instances simultaneously
for i in {1..5}; do
    (echo "Client $i test" | nc 127.0.0.1 6767) &
done
wait
