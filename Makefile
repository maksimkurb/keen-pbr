BUILD_DIR := build
DIST_DIR := dist
DOCKER_IMAGE := keen-pbr3-builder

ARCHS := mips-be-openwrt mips-le-openwrt arm-openwrt aarch64-openwrt x86_64-openwrt mips-le-keenetic

SETUP_STAMP := $(BUILD_DIR)/.stamp-setup

.PHONY: all build clean distclean setup \
        docker-image docker-all $(addprefix docker-,$(ARCHS)) \
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
	docker build -f docker/Dockerfile.openwrt -t $(DOCKER_IMAGE) .

docker-all: docker-image $(addprefix docker-,$(ARCHS)) ## Build for all architectures via Docker

define DOCKER_ARCH_RULE
docker-$(1): docker-image ## Build for $(1) via Docker
	docker run --rm -v "$$(pwd)/$(DIST_DIR):/src/$(DIST_DIR)" $(DOCKER_IMAGE) $(1)
endef

$(foreach arch,$(ARCHS),$(eval $(call DOCKER_ARCH_RULE,$(arch))))

## Help ########################################################################

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
