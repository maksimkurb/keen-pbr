BUILD_DIR := build
DIST_DIR := dist
DOCKER_IMAGE := keen-pbr3-builder

SETUP_STAMP := $(BUILD_DIR)/.stamp-setup

.PHONY: all build clean distclean setup \
        docker-image docker-extract \
        help

## Native build ################################################################

all: build ## Build for host (native)

$(SETUP_STAMP): meson.build
	meson setup $(BUILD_DIR) --wipe 2>/dev/null \
		|| meson setup $(BUILD_DIR)
	@touch $@

setup: $(SETUP_STAMP) ## Configure Meson

build: $(SETUP_STAMP) ## Compile the project
	meson compile -C $(BUILD_DIR)

clean: ## Remove build artifacts
	rm -rf $(BUILD_DIR)

distclean: clean ## Remove build and dist directories
	rm -rf $(DIST_DIR)

## Docker cross-compilation ####################################################

docker-image: ## Build the Docker builder image
	docker build -f docker/Dockerfile.packages -t $(DOCKER_IMAGE) .

docker-extract: docker-image ## Extract .ipk packages from Docker image
	@mkdir -p $(DIST_DIR)
	docker create --name keen-pbr3-tmp $(DOCKER_IMAGE) 2>/dev/null || true
	docker cp keen-pbr3-tmp:/home/me/openwrt/bin/packages/. $(DIST_DIR)/
	docker rm keen-pbr3-tmp

## Help ########################################################################

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
