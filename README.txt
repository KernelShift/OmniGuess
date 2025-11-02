OmniGuess — SECP256K1 Hunter (Multi-thread, No System Installs for Users)

What it does
------------
Reads compressed public keys (66 hex chars, starting with 02/03) from compressed_pubkeys.txt,
builds an in-memory hashtable, then generates random secp256k1 private keys, derives compressed
pubkeys, and checks for a match. On the first match, it prints the keypair and appends it to
matches.txt as "priv_hex,pub_hex".

Project layout (you should already have this)
---------------------------------------------
.
├─ main.c
├─ hashtable.c
├─ hashtable.h
├─ compressed_pubkeys.txt
└─ secp256k1/                  (Bitcoin Core secp256k1 source; unbuilt initially)

Build quickstart (macOS universal arm64 + x86_64)
-------------------------------------------------
1) Open Terminal in the project directory.
2) Run:
   make mac-universal

This will:
- Build libsecp256k1 twice (arm64 and x86_64)
- Lipo them into a universal static archive
- Link the final universal binary: OmniGuess-macos-universal
- Strip it for size

Usage
-----
./OmniGuess-macos-universal --threads=8
./OmniGuess-macos-universal --threads=8 --quiet
./OmniGuess-macos-universal --db=compressed_pubkeys.txt --threads=16 --ui-interval=1.0

Flags:
--db=PATH           Path to database file of compressed pubkeys (default: compressed_pubkeys.txt)
--threads=N         Number of worker threads (default: 1)
--ui-interval=SEC   UI refresh interval (default: 0.25)
--quiet             Disable full-screen UI, prints only on match or exit

Output files:
- matches.txt       On a match: a line "PRIVHEX,PUBHEX" is appended
- SHA256SUMS        Created by "make verify" with checksums of built binaries

Verifying your build
--------------------
1) Check universal slices:
   lipo -info OmniGuess-macos-universal

2) Generate checksums:
   make verify
   cat SHA256SUMS

Linux build (on a Linux machine or container)
---------------------------------------------
1) Build the vendored secp256k1:
   cd secp256k1
   ./autogen.sh
   ./configure --disable-shared --enable-static CFLAGS="-O3"
   make -j
   cd ..

2) Build the app:
   make linux

This produces OmniGuess-linux-x64. Run:
   ./OmniGuess-linux-x64 --threads=8

Windows build (MinGW-w64 cross, from macOS/Linux)
-------------------------------------------------
1) First build secp256k1 with MinGW or copy a prebuilt libsecp256k1.a for MinGW into secp256k1/.libs/
2) Build the app:
   make win

This produces OmniGuess-win-x64.exe.

Security & RNG
--------------
- macOS: Security.framework (SecRandomCopyBytes)
- Linux: getrandom() or /dev/urandom
- Windows: BCryptGenRandom (link with -lbcrypt)
- We generate valid private keys and derive compressed pubkeys via Bitcoin Core secp256k1.

Performance tips
----------------
- Use --quiet to avoid full-screen UI overhead.
- Increase --ui-interval to 1.0 for fewer redraws.
- Scale threads with --threads=N (roughly linear with logical cores).

How the universal binary was built
----------------------------------
- We configure & build secp256k1 twice: once with -arch arm64 and once with -arch x86_64.
- We combine the two static archives using lipo into libsecp256k1-universal.a.
- We compile main.c + hashtable.c with -arch arm64 -arch x86_64 and link against libsecp256k1-universal.a,
  producing OmniGuess-macos-universal.

One-liners
----------
# Build universal and verify:
make mac-universal && make verify

# Run:
./OmniGuess-macos-universal --threads=8

# Package:
make dist
