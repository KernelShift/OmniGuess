# OmniGuess — portable secp256k1 hunter
# Requires: Xcode CLT (macOS) / GCC or Clang (Linux/Windows cross), autotools in secp256k1/
# Assumes: secp256k1/ folder is present but UNBUILT.

# ---- Project files ----
SRC_APP   := main.c hashtable.c
HDR_APP   := hashtable.h
DB_FILE   := compressed_pubkeys.txt

# ---- Paths to vendored libsecp256k1 ----
SECP_DIR  := secp256k1
SECP_INC  := $(SECP_DIR)/include

# Built slice outputs
SECP_A_ARM64   := $(SECP_DIR)/build-arm64/.libs/libsecp256k1.a
SECP_A_X64     := $(SECP_DIR)/build-x86_64/.libs/libsecp256k1.a
SECP_A_UNI     := $(SECP_DIR)/libsecp256k1-universal.a

# ---- Outputs ----
BIN_MAC_ARM64  := OmniGuess-arm64
BIN_MAC_X64    := OmniGuess-x86_64
BIN_MAC_UNI    := OmniGuess-macos-universal
BIN_LINUX_X64  := OmniGuess-linux-x64
BIN_WIN_X64    := OmniGuess-win-x64.exe

# ---- Tools ----
CC       ?= cc
CROSS_CC ?= x86_64-w64-mingw32-gcc
STRIP    ?= strip

# ---- Flags ----
CFLAGS   := -O3 -std=c11 -Wall -Wextra -pedantic -I$(SECP_INC)
LDFLAGS  := 
LIBS_MAC := -framework Security -pthread
LIBS_POS := -pthread

.PHONY: all mac mac-universal mac-arm64 mac-x86_64 linux win verify checksums dist clean superclean help

all: mac-universal

help:
	@echo "Targets:"
	@echo "  mac-universal   Build universal macOS binary (arm64 + x86_64)"
	@echo "  mac-arm64       Build macOS Apple Silicon only"
	@echo "  mac-x86_64      Build macOS Intel only"
	@echo "  linux           Build Linux x86_64 (link against built .a in secp256k1/.libs)"
	@echo "  win             Build Windows x86_64 (MinGW-w64 cross)"
	@echo "  verify          Verify universal slices + create SHA256SUMS"
	@echo "  checksums       Create SHA256SUMS for all present binaries"
	@echo "  dist            Build all, verify, and package (zip)"
	@echo "  clean           Clean app binaries"
	@echo "  superclean      Clean everything including secp256k1 builds"

# ---------------------------
# macOS: build vendor lib (arm64 + x86_64), lipo into universal, link app
# ---------------------------

$(SECP_A_ARM64):
	cd $(SECP_DIR) && ./autogen.sh
	mkdir -p $(SECP_DIR)/build-arm64
	cd $(SECP_DIR)/build-arm64 && CC="clang" CFLAGS="-O3 -arch arm64" LDFLAGS="-arch arm64" ../configure --disable-shared --enable-static
	$$(MAKE) -C $(SECP_DIR)/build-arm64 -j

$(SECP_A_X64):
	test -f $(SECP_DIR)/configure || (cd $(SECP_DIR) && ./autogen.sh)
	mkdir -p $(SECP_DIR)/build-x86_64
	cd $(SECP_DIR)/build-x86_64 && CC="clang" CFLAGS="-O3 -arch x86_64" LDFLAGS="-arch x86_64" ../configure --disable-shared --enable-static
	$$(MAKE) -C $(SECP_DIR)/build-x86_64 -j

$(SECP_A_UNI): $(SECP_A_ARM64) $(SECP_A_X64)
	lipo -create -output $(SECP_A_UNI) $(SECP_A_ARM64) $(SECP_A_X64)
	lipo -info $(SECP_A_UNI)

mac-universal: $(SECP_A_UNI) $(SRC_APP) $(HDR_APP)
	$(CC) -O3 -std=c11 -Wall -Wextra -pedantic -I$(SECP_INC) \
	  -arch arm64 -arch x86_64 \
	  $(SRC_APP) $(SECP_A_UNI) \
	  $(LIBS_MAC) \
	  -o $(BIN_MAC_UNI)
	$(STRIP) -S $(BIN_MAC_UNI)
	@echo "Built $(BIN_MAC_UNI)"

mac-arm64: $(SECP_A_ARM64) $(SRC_APP) $(HDR_APP)
	$(CC) $(CFLAGS) -arch arm64 $(SRC_APP) $(SECP_A_ARM64) $(LIBS_MAC) -o $(BIN_MAC_ARM64)
	$(STRIP) -S $(BIN_MAC_ARM64)
	@echo "Built $(BIN_MAC_ARM64)"

mac-x86_64: $(SECP_A_X64) $(SRC_APP) $(HDR_APP)
	$(CC) $(CFLAGS) -arch x86_64 $(SRC_APP) $(SECP_A_X64) $(LIBS_MAC) -o $(BIN_MAC_X64)
	$(STRIP) -S $(BIN_MAC_X64)
	@echo "Built $(BIN_MAC_X64)"

# ---------------------------
# Linux build (expects you to build secp256k1 on Linux first)
# ---------------------------
linux: $(SRC_APP) $(HDR_APP)
	@echo "Ensure secp256k1 is built on Linux: ./autogen.sh && ./configure --disable-shared --enable-static && make -j"
	$(CC) $(CFLAGS) $(SRC_APP) $(SECP_DIR)/.libs/libsecp256k1.a $(LIBS_POS) -o $(BIN_LINUX_X64)
	$(STRIP) $(BIN_LINUX_X64)
	@echo "Built $(BIN_LINUX_X64)"

# ---------------------------
# Windows build (MinGW-w64 cross)
# ---------------------------
win: $(SRC_APP) $(HDR_APP)
	$(CROSS_CC) -O3 -std=c11 -Wall -Wextra -pedantic -I$(SECP_INC) \
	  $(SRC_APP) $(SECP_DIR)/.libs/libsecp256k1.a \
	  -lbcrypt -lws2_32 -o $(BIN_WIN_X64)
	@echo "Built $(BIN_WIN_X64)"

# ---------------------------
# Verify and package
# ---------------------------
verify:
	@which shasum >/dev/null 2>&1 || which sha256sum >/dev/null 2>&1 || (echo "Install shasum/sha256sum"; exit 2)
	@test -f $(BIN_MAC_UNI) && lipo -info $(BIN_MAC_UNI) || true
	@rm -f SHA256SUMS
	@if test -f $(BIN_MAC_UNI); then shasum -a 256 $(BIN_MAC_UNI) >> SHA256SUMS; fi
	@if test -f $(BIN_MAC_ARM64); then shasum -a 256 $(BIN_MAC_ARM64) >> SHA256SUMS; fi
	@if test -f $(BIN_MAC_X64); then shasum -a 256 $(BIN_MAC_X64) >> SHA256SUMS; fi
	@if test -f $(BIN_LINUX_X64); then shasum -a 256 $(BIN_LINUX_X64) >> SHA256SUMS; fi
	@if test -f $(BIN_WIN_X64); then shasum -a 256 $(BIN_WIN_X64) >> SHA256SUMS; fi
	@echo "Wrote SHA256SUMS"

checksums: verify

dist: mac-universal
	$(MAKE) verify
	zip -9 OmniGuess-macos-universal.zip $(BIN_MAC_UNI) SHA256SUMS
	@echo "Created OmniGuess-macos-universal.zip"

# ---------------------------
# Cleaning
# ---------------------------
clean:
	rm -f $(BIN_MAC_UNI) $(BIN_MAC_ARM64) $(BIN_MAC_X64) $(BIN_LINUX_X64) $(BIN_WIN_X64) SHA256SUMS OmniGuess-*.zip

superclean: clean
	@if [ -d "$(SECP_DIR)" ]; then cd $(SECP_DIR) && git clean -fdx; fi
