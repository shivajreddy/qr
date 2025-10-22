#!/usr/bin/env bash
set -e # Exit on any error

# === CONFIG ===
CXX=g++
CXXFLAGS="-std=c++23 -Wall -Wextra"
SRC_DIR="src"
BUILD_DIR="build"
OUT="$BUILD_DIR/main"

# Include directories
INCLUDES="-Iinclude"

# Libraries to Link
LIBS="-lcurl"
# LIBS="-lcurl -lssl -lcrypto"

# === BUILD ===
mkdir -p "$BUILD_DIR"

# Compile all .cpp files recursively
$CXX $CXXFLAGS $INCLUDES $(find "$SRC_DIR" -name "*.cpp") -o "$OUT" $LIBS

# Run the binary
./build/main
