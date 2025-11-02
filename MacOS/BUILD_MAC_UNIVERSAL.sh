#!/usr/bin/env bash
set -euo pipefail

cd secp256k1
./autogen.sh

mkdir -p build-arm64
cd build-arm64
CC=clang CFLAGS="-O3 -arch arm64" LDFLAGS="-arch arm64" ../configure --disable-shared --enable-static
make -j
cd ..

mkdir -p build-x86_64
cd build-x86_64
CC=clang CFLAGS="-O3 -arch x86_64" LDFLAGS="-arch x86_64" ../configure --disable-shared --enable-static
make -j
cd ..

lipo -create -output libsecp256k1-universal.a \
  build-arm64/.libs/libsecp256k1.a \
  build-x86_64/.libs/libsecp256k1.a
lipo -info libsecp256k1-universal.a

cd ..
cc -O3 -std=c11 -Wall -Wextra -pedantic -I./secp256k1/include \
  -arch arm64 -arch x86_64 \
  main.c hashtable.c ./secp256k1/libsecp256k1-universal.a \
  -framework Security -pthread \
  -o OmniGuess-macos-universal

strip -S OmniGuess-macos-universal
echo "Built OmniGuess-macos-universal"
