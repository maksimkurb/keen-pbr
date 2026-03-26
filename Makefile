BUILD_DIR := cmake-build
DIST_DIR := dist
ORVAL_VERSION := $(shell sed -n 's/.*"orval": "\([^"]*\)".*/\1/p' frontend/package.json | head -1)

# Prefer an explicitly installed compiler when available; C++17 is required.
CXX := $(shell command -v g++-13 2>/dev/null || command -v g++-12 2>/dev/null || command -v g++ 2>/dev/null || echo g++)
CMAKE_CXX_FLAGS := -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

SETUP_STAMP := $(BUILD_DIR)/.stamp-setup

.PHONY: all build clean distclean setup \
        frontend-build \
        frontend-api-generate \
        test \
        generate \
        cross-setup cross-build cross-deploy \
        help

## Native build ################################################################

all: build ## Build for host (native)

$(SETUP_STAMP): CMakeLists.txt
	cmake -S . -B $(BUILD_DIR) $(CMAKE_CXX_FLAGS)
	@touch $@

setup: $(SETUP_STAMP) ## Configure CMake

build: $(SETUP_STAMP) ## Compile the project
	cmake --build $(BUILD_DIR)

frontend-build: ## Build frontend assets with bun
	mkdir -p /tmp/keen-pbr-bun-tmp /tmp/keen-pbr-bun-cache /tmp/keen-pbr-frontend-dist
	cd frontend && \
	TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache bun install --frozen-lockfile && \
	KEEN_PBR_FRONTEND_OUT_DIR=/tmp/keen-pbr-frontend-dist TMPDIR=/tmp/keen-pbr-bun-tmp TEMP=/tmp/keen-pbr-bun-tmp TMP=/tmp/keen-pbr-bun-tmp BUN_INSTALL_CACHE_DIR=/tmp/keen-pbr-bun-cache bun run build

frontend-api-generate: ## Regenerate frontend API client using the Orval version pinned in frontend/package.json
	cd frontend && bunx --bun orval@$(ORVAL_VERSION) --config ./orval.config.ts

generate: ## Regenerate src/api/generated/api_types.hpp from docs/openapi.yaml (requires Node.js)
	bash scripts/generate_api_types.sh

test: ## Build and run unit tests (doctest)
	cmake -S . -B $(BUILD_DIR) $(CMAKE_CXX_FLAGS) -DBUILD_TESTS=ON
	cmake --build $(BUILD_DIR) --target keen-pbr-tests
	$(BUILD_DIR)/tests/keen-pbr-tests

clean: ## Remove build artifacts
	rm -rf $(BUILD_DIR)

distclean: clean ## Remove build and dist directories
	rm -rf $(DIST_DIR)

## Cross-compilation for aarch64_cortex-a53 + deploy ##########################

CROSS_TOOLCHAIN_DIR   := cross-toolchain
CROSS_TOOLCHAIN_STAMP := $(CROSS_TOOLCHAIN_DIR)/.stamp-extracted
CROSS_BUILD_DIR       := cmake-build-aarch64
CROSS_BIN             := $(DIST_DIR)/keen-pbr-aarch64
CROSS_DEBUG_BIN       := $(DIST_DIR)/keen-pbr-aarch64.debug
CROSS_OBJCOPY         := $(shell ls $(CROSS_TOOLCHAIN_DIR)/toolchain-*/bin/aarch64-openwrt-linux-musl-objcopy 2>/dev/null | head -1)
# runas.so (LD_PRELOAD'd by the compiler wrapper) uses STAGING_DIR to redirect
# the compiler's baked-in absolute sysroot paths to the actual extracted location.
# Without this, the compiler falls through to the host /usr/include (glibc).
CROSS_STAGING_DIR     := $(abspath $(CROSS_TOOLCHAIN_DIR))/sdk/staging_dir

# URL of the OpenWrt SDK .tar.zst for an aarch64_cortex-a53 target.
# Example: https://downloads.openwrt.org/releases/24.10.4/targets/rockchip/armv8/openwrt-sdk-24.10.4-rockchip-armv8_gcc-13.3.0_musl.Linux-x86_64.tar.zst
OPENWRT_SDK_URL ?= https://archive.openwrt.org/releases/24.10.3/targets/mediatek/filogic/openwrt-sdk-24.10.3-mediatek-filogic_gcc-13.3.0_musl.Linux-x86_64.tar.zst

# SFTP deploy settings — pass as environment variables
ROUTER_HOST ?=
ROUTER_USER ?= root
ROUTER_PORT ?= 22
ROUTER_DEST ?= /tmp/keen-pbr

cross-setup: ## Download OpenWrt SDK and build cross-compile deps (no Docker; requires OPENWRT_SDK_URL)
	@test -n "$(OPENWRT_SDK_URL)" || { \
		echo "Error: OPENWRT_SDK_URL is not set."; \
		echo "Example: https://downloads.openwrt.org/releases/24.10.4/targets/rockchip/armv8/openwrt-sdk-24.10.4-rockchip-armv8_gcc-13.3.0_musl.Linux-x86_64.tar.zst"; \
		exit 1; }
	OPENWRT_SDK_URL=$(OPENWRT_SDK_URL) packages/cross-setup.sh $(CROSS_TOOLCHAIN_DIR)

$(CROSS_TOOLCHAIN_STAMP):
	$(MAKE) cross-setup

cross-build: $(CROSS_TOOLCHAIN_STAMP) ## Cross-compile for aarch64_cortex-a53 directly (fast, no Docker)
	STAGING_DIR=$(CROSS_STAGING_DIR) cmake -S . -B $(CROSS_BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-openwrt.cmake \
		-DCMAKE_BUILD_TYPE=MinSizeRel \
		-DCMAKE_CXX_FLAGS_MINSIZEREL="-Os -DNDEBUG -g1" \
		-DWITH_API=ON
	STAGING_DIR=$(CROSS_STAGING_DIR) cmake --build $(CROSS_BUILD_DIR) -j$(shell nproc)
	@mkdir -p $(DIST_DIR)
	# Extract full debug symbols into a separate .debug file (stays on the host)
	$(CROSS_OBJCOPY) --only-keep-debug $(CROSS_BUILD_DIR)/keen-pbr $(CROSS_DEBUG_BIN)
	# Strip DWARF from the deployed binary (symbol table kept — backtrace() still resolves names)
	$(CROSS_OBJCOPY) --strip-debug --add-gnu-debuglink=$(CROSS_DEBUG_BIN) \
		$(CROSS_BUILD_DIR)/keen-pbr $(CROSS_BIN)
	@echo "Binary:        $(CROSS_BIN)"
	@echo "Debug symbols: $(CROSS_DEBUG_BIN)"
	@echo "Resolve crash addresses: addr2line -e $(CROSS_DEBUG_BIN) <address>"

cross-deploy: cross-build ## Cross-compile and upload binary to router via SFTP (requires ROUTER_HOST)
	@test -n "$(ROUTER_HOST)" || { echo "Error: ROUTER_HOST is not set"; exit 1; }
	@echo "Uploading to $(ROUTER_USER)@$(ROUTER_HOST):$(ROUTER_DEST) ..."
	printf 'put $(CROSS_BIN) $(ROUTER_DEST)\nchmod 755 $(ROUTER_DEST)\n' | \
		sftp -P $(ROUTER_PORT) $(ROUTER_USER)@$(ROUTER_HOST)
	@echo "Done."

-include docker/build.mk

## Help ########################################################################

help: ## Show this help
	@grep -hE '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
