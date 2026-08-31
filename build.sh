#!/bin/bash

set -e

cmake -S . -B build
cmake --build build -j

ln -sf build/backend/server ./server

echo "Created symlink: ./server -> build/backend/server"