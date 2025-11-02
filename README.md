# OmniGuess 🔑  
**A fast, multi-threaded SECP256K1 key hunter written in C**

OmniGuess reads a file of **compressed public keys** (`compressed_pubkeys.txt`), builds an in-memory hash table, then continuously generates random SECP256K1 private keys, derives compressed pubkeys, and checks for matches — stopping on the first hit.  
It’s written for speed, portability, and transparency.

---

## 🧱 Features
- **Fully offline**, no network or API dependencies  
- **Multi-threaded** with dynamic UI or quiet mode  
- **Cross-platform:** macOS, Linux, Windows  
- **Uses Bitcoin Core’s audited `libsecp256k1`** for secure elliptic curve math  
- **Single universal binary (macOS)** – works on both Intel and Apple Silicon  
- **Outputs matched keypairs** to `matches.txt`  
- **Verifiable build:** SHA256 + lipo architecture checks

---

## 📂 Project Structure
```
standalone/
├── BUILD_MAC_UNIVERSAL.sh     # One-click macOS universal builder
├── Makefile                   # Cross-platform build targets
├── README.txt                 # Local text readme
├── VERIFY.sh                  # Architecture & checksum verifier
├── hashtable.c / hashtable.h  # In-memory hash table
├── main.c                     # Core OmniGuess logic
├── compressed_pubkeys.txt     # Input database of pubkeys
├── secp256k1/                 # Bitcoin Core SECP256K1 source
├── dist/                      # Built binaries & checksums
└── SHA256SUMS                 # Verification hashes
```

---

## ⚙️ Building OmniGuess

### macOS Universal (Intel + Apple Silicon)
```bash
make mac-universal
make verify
./dist/OmniGuess-macos-universal --threads=8
```

The Makefile automatically:
- Builds `libsecp256k1` for both `arm64` and `x86_64`
- Combines them with `lipo`
- Links your final **universal** binary

### macOS Single-Arch
```bash
make mac-arm64   # Apple Silicon only
make mac-x86_64  # Intel only
```

### Linux
```bash
cd secp256k1
./autogen.sh && ./configure --disable-shared --enable-static && make -j
cd ..
make linux
./OmniGuess-linux-x64 --threads=8
```

### Windows (MinGW-w64 cross-compile)
```bash
make win
OmniGuess-win-x64.exe --threads=8
```

---

## ✅ Verifying Your Build
```bash
./VERIFY.sh
```
This prints architectures and SHA256 checksum.  
Or manually:
```bash
lipo -info OmniGuess-macos-universal
shasum -a 256 OmniGuess-macos-universal
```

Example:
```
Architectures in the fat file: OmniGuess-macos-universal are: x86_64 arm64
a6f4d7c752d241dc259791b00839ee8c13bed11ab3c7267550d6ffb25d93385d  OmniGuess-macos-universal
```

---

## 🚀 Usage
```bash
./OmniGuess-macos-universal --threads=8
```

### Options
| Flag | Description |
|------|--------------|
| `--db=PATH` | Path to `compressed_pubkeys.txt` |
| `--threads=N` | Number of worker threads |
| `--ui-interval=SEC` | Refresh interval for on-screen stats |
| `--quiet` | Disable live UI for speed |
| `--no-validate-db` | Skip input validation (faster startup) |

### Output
When a match is found:
```
FOUND MATCH!
Private key: <hex>
Pub (cmp):  <hex>
Saved to matches.txt
```

---

## 📈 Performance
Typical macOS M-series:
```
Threads: 8
Rate: ~600,000 keys/sec (~2.1 billion/hr)
```
Use `--quiet` for slightly higher rates.

---

## 🔒 Security & RNG Sources
- macOS → `SecRandomCopyBytes`
- Linux → `getrandom()` → `/dev/urandom`
- Windows → `BCryptGenRandom`
- SECP256K1 computations via **Bitcoin Core’s `libsecp256k1`**

---

## 🧰 Reproducible Universal Build Explained
```bash
# Inside secp256k1/
git checkout v0.6.0
git tag -v v0.6.0 | grep -C 3 'Good signature'
./autogen.sh
mkdir -p build-arm64 && cd build-arm64
CC=clang CFLAGS="-O3 -arch arm64" LDFLAGS="-arch arm64" ../configure --disable-shared --enable-static
make -j && cd ..
mkdir -p build-x86_64 && cd build-x86_64
CC=clang CFLAGS="-O3 -arch x86_64" LDFLAGS="-arch x86_64" ../configure --disable-shared --enable-static
make -j && cd ..
lipo -create -output libsecp256k1-universal.a build-arm64/.libs/libsecp256k1.a build-x86_64/.libs/libsecp256k1.a
lipo -info libsecp256k1-universal.a
cd ..
cc -O3 -std=c11 -Wall -Wextra -pedantic -I./secp256k1/include -arch arm64 -arch x86_64 main.c hashtable.c ./secp256k1/libsecp256k1-universal.a -framework Security -pthread -o OmniGuess-macos-universal
strip -S OmniGuess-macos-universal
```

Result:  
`OmniGuess-macos-universal` ✅  
`Architectures: x86_64 arm64`

---

## 📜 License
OmniGuess is released under the **MIT License**.  
`libsecp256k1` remains under its own license (BSD-style).

---

## 💡 Credits
- SECP256K1 math via Bitcoin Core developers  
- Hash table base from @goldsborough/hashtable  
- OmniGuess built & optimized by **Logan Ercanbrack**
