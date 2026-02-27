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

## Help ########################################################################

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
