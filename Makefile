BUILD_DIR := cmake-build
DIST_DIR := dist
DOCKER_IMAGE := keen-pbr3-builder

# Prefer GCC 13+ for C++20 <format> support; fall back to default g++
CXX := $(shell command -v g++-13 2>/dev/null || command -v g++ 2>/dev/null || echo g++)
CMAKE_CXX_FLAGS := -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

SETUP_STAMP := $(BUILD_DIR)/.stamp-setup

.PHONY: all build clean distclean setup \
        test \
        docker-image docker-extract \
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

test: ## Build and run unit tests (doctest)
	cmake -S . -B $(BUILD_DIR) $(CMAKE_CXX_FLAGS) -DBUILD_TESTS=ON
	cmake --build $(BUILD_DIR) --target keen-pbr3-tests
	$(BUILD_DIR)/tests/keen-pbr3-tests

clean: ## Remove build artifacts
	rm -rf $(BUILD_DIR)

distclean: clean ## Remove build and dist directories
	rm -rf $(DIST_DIR)

## Docker cross-compilation ####################################################

docker-image: ## Build the Docker builder image
	 docker build -f docker/Dockerfile.packages -t $(DOCKER_IMAGE) --build-arg ROUTER_CONFIG=$(ROUTER_CONFIG) .

docker-extract: docker-image ## Extract .ipk packages from Docker image
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)
	docker rm keen-pbr3-tmp 2>/dev/null || true
	docker create --name keen-pbr3-tmp $(DOCKER_IMAGE)
	docker cp keen-pbr3-tmp:/home/me/openwrt/bin/packages/. $(DIST_DIR)/
	docker rm keen-pbr3-tmp

## Cross-compilation for aarch64_cortex-a53 + deploy ##########################

CROSS_TOOLCHAIN_DIR   := cross-toolchain
CROSS_TOOLCHAIN_STAMP := $(CROSS_TOOLCHAIN_DIR)/.stamp-extracted
CROSS_BUILD_DIR       := cmake-build-aarch64
CROSS_BIN             := $(DIST_DIR)/keen-pbr3-aarch64
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
ROUTER_DEST ?= /tmp/keen-pbr3

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
		-DWITH_API=ON
	STAGING_DIR=$(CROSS_STAGING_DIR) cmake --build $(CROSS_BUILD_DIR) -j$(shell nproc)
	@mkdir -p $(DIST_DIR)
	cp $(CROSS_BUILD_DIR)/keen-pbr3 $(CROSS_BIN)
	@echo "Binary: $(CROSS_BIN)"

cross-deploy: cross-build ## Cross-compile and upload binary to router via SFTP (requires ROUTER_HOST)
	@test -n "$(ROUTER_HOST)" || { echo "Error: ROUTER_HOST is not set"; exit 1; }
	@echo "Uploading to $(ROUTER_USER)@$(ROUTER_HOST):$(ROUTER_DEST) ..."
	printf 'put $(CROSS_BIN) $(ROUTER_DEST)\nchmod 755 $(ROUTER_DEST)\n' | \
		sftp -P $(ROUTER_PORT) $(ROUTER_USER)@$(ROUTER_HOST)
	@echo "Done."

## Help ########################################################################

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
