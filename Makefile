include version.mk

VERSION_RESOLVER := $(abspath build_scripts/resolve-version.sh)
KEEN_PBR_RELEASE := $(shell bash $(VERSION_RESOLVER) release "$(CURDIR)")
GCC_BUILD_DIR := cmake-build-gcc
CLANG_BUILD_DIR := cmake-build-clang
DIST_DIR := build/dist
ORVAL_VERSION := $(shell sed -n 's/.*"orval": "\([^"]*\)".*/\1/p' frontend/package.json | head -1)
KEEN_PBR_VERSION_RELEASE := $(KEEN_PBR_VERSION)-$(KEEN_PBR_RELEASE)

# Prefer an explicitly installed compiler when available; C++17 is required.
GCC_CXX ?= $(shell command -v g++-13 2>/dev/null || command -v g++-12 2>/dev/null || command -v g++ 2>/dev/null || echo g++)
CLANG_CXX ?= clang++
COMMON_CMAKE_FLAGS := -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DKEEN_PBR_RELEASE=$(KEEN_PBR_RELEASE)
GCC_CMAKE_FLAGS := -DCMAKE_CXX_COMPILER=$(GCC_CXX) $(COMMON_CMAKE_FLAGS)
CLANG_CMAKE_FLAGS := -DCMAKE_CXX_COMPILER=$(CLANG_CXX) $(COMMON_CMAKE_FLAGS)
CLANG_FEATURE_CMAKE_FLAGS := -DWITH_API=ON -DUSE_KEENETIC_API=ON

.PHONY: all build clean distclean setup \
        frontend-build \
        frontend-api-generate \
        test \
        clang-build clang-check clang-tidy \
        generate \
        cross-setup cross-build cross-deploy \
        help

## Native build ################################################################

all: build ## Build for host (native)

setup: ## Configure CMake
	cmake -S . -B $(GCC_BUILD_DIR) $(GCC_CMAKE_FLAGS)

build: ## Compile the project
	cmake -S . -B $(GCC_BUILD_DIR) $(GCC_CMAKE_FLAGS)
	cmake --build $(GCC_BUILD_DIR)

frontend-build: ## Build frontend assets with bun
	bash build_scripts/build-frontend.sh "$(abspath .)" "$(abspath frontend/dist)"

frontend-api-generate: ## Regenerate frontend API client using the Orval version pinned in frontend/package.json
	cd frontend && bunx --bun orval@$(ORVAL_VERSION) --config ./orval.config.ts

generate: ## Regenerate src/api/generated/api_types.hpp from docs/openapi.yaml (requires Node.js)
	bash build_scripts/generate_api_types.sh

test: ## Build and run unit tests (doctest)
	cmake -S . -B $(GCC_BUILD_DIR) $(GCC_CMAKE_FLAGS) -DBUILD_TESTS=ON
	cmake --build $(GCC_BUILD_DIR) --target keen-pbr-tests
	$(GCC_BUILD_DIR)/tests/keen-pbr-tests

clang-build: ## Configure and compile with Clang in a host-only build dir
	cmake -S . -B $(CLANG_BUILD_DIR) $(CLANG_CMAKE_FLAGS) $(CLANG_FEATURE_CMAKE_FLAGS)
	cmake --build $(CLANG_BUILD_DIR) --target keen-pbr

clang-check: ## Compile with Clang thread-safety analysis enabled; never runs binaries
	cmake -S . -B $(CLANG_BUILD_DIR) $(CLANG_CMAKE_FLAGS) $(CLANG_FEATURE_CMAKE_FLAGS) -DBUILD_TESTS=ON -DENABLE_THREAD_SAFETY_ANALYSIS=ON
	cmake --build $(CLANG_BUILD_DIR) --target keen-pbr keen-pbr-tests thread-safety-smoke

CLANGD_TIDY_ARGS ?=

clang-tidy: ## Run clangd-tidy against project-owned sources using the Clang compile database
	cmake -S . -B $(CLANG_BUILD_DIR) $(CLANG_CMAKE_FLAGS) $(CLANG_FEATURE_CMAKE_FLAGS) -DBUILD_TESTS=ON -DENABLE_THREAD_SAFETY_ANALYSIS=ON
	bash build_scripts/run-clangd-tidy.sh "$(abspath $(CLANG_BUILD_DIR))" $(CLANGD_TIDY_ARGS)

clean: ## Remove compiled artifacts
	rm -rf $(GCC_BUILD_DIR) $(CLANG_BUILD_DIR) build/cmake-aarch64 build/cross-toolchain build/dist \
	       build/debian-src-full build/debian-src-headless build/packages

distclean: ## Remove all build artifacts including downloaded SDKs
	rm -rf build/ $(GCC_BUILD_DIR) $(CLANG_BUILD_DIR)

## Cross-compilation for aarch64_cortex-a53 + deploy ##########################

CROSS_TOOLCHAIN_DIR   := build/cross-toolchain
CROSS_TOOLCHAIN_STAMP := $(CROSS_TOOLCHAIN_DIR)/.stamp-extracted
CROSS_BUILD_DIR       := build/cmake-aarch64
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
-include build_scripts/openwrt.mk
-include build_scripts/keenetic.mk
-include build_scripts/debian.mk
-include build_scripts/repository.mk

## Help ########################################################################

help: ## Show this help
	@grep -hE '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
