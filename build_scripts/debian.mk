## Debian packaging #############################################################
#
# Variables (all overridable via environment or CLI):
#   DEBIAN_DOCKER_IMAGE    — Builder image
#   DEBIAN_VERSION         — Debian distribution name for build/packages layout
#   DEBIAN_DOCKER_CACHE_FROM / DEBIAN_DOCKER_CACHE_TO — Optional buildx cache dirs

DEBIAN_DOCKER_IMAGE ?= keen-pbr-debian-builder:latest
DEBIAN_VERSION ?= bullseye
DEBIAN_DOCKER_CACHE_FROM ?=
DEBIAN_DOCKER_CACHE_TO   ?=
DEBIAN_VARIANTS ?= full headless
DEBIAN_CCACHE_DIR ?= $(abspath build/ccache/$(DEBIAN_VERSION))

.PHONY: deb-packages debian-builder-image

deb-packages: ## Build Debian packages inside Docker container
	@echo "[deb-packages] config: DEBIAN_VERSION=$(DEBIAN_VERSION) DEBIAN_DOCKER_IMAGE=$(DEBIAN_DOCKER_IMAGE)"
	@$(MAKE) debian-builder-image
	mkdir -p build/packages "$(DEBIAN_CCACHE_DIR)"
	docker run --rm \
	  -e KEEN_PBR_RELEASE_OVERRIDE="$(KEEN_PBR_RELEASE)" \
	  -e DEBIAN_VARIANTS="$(DEBIAN_VARIANTS)" \
	  -e CCACHE_DIR=/ccache \
	  -e CCACHE_BASEDIR=/workspace \
	  -e CCACHE_NOHASHDIR=true \
	  -v "$(abspath .):/workspace" \
	  -v "$(DEBIAN_CCACHE_DIR):/ccache" \
	  "$(DEBIAN_DOCKER_IMAGE)" \
	  bash -c 'set -e; \
	    git config --global --add safe.directory /workspace; \
	    DEBIAN_VERSION="$(DEBIAN_VERSION)" \
	    bash /workspace/build_scripts/build-debian-packages.sh /workspace /workspace/build/packages'

debian-builder-image: ## Build the Debian builder Docker image locally
	@echo "[debian-builder-image] config: DEBIAN_VERSION=$(DEBIAN_VERSION) DEBIAN_DOCKER_IMAGE=$(DEBIAN_DOCKER_IMAGE) DEBIAN_DOCKER_CACHE_FROM=$(DEBIAN_DOCKER_CACHE_FROM) DEBIAN_DOCKER_CACHE_TO=$(DEBIAN_DOCKER_CACHE_TO)"
	@docker buildx version >/dev/null 2>&1 || { echo "Docker Buildx is required; install the docker-buildx-plugin." >&2; exit 2; }
	@if [ -n "$(DEBIAN_DOCKER_CACHE_FROM)" ] || [ -n "$(DEBIAN_DOCKER_CACHE_TO)" ]; then \
	  cache_args=""; \
	  if [ -n "$(DEBIAN_DOCKER_CACHE_FROM)" ]; then \
	    cache_args="$$cache_args --cache-from type=local,src=$(DEBIAN_DOCKER_CACHE_FROM)"; \
	  fi; \
	  if [ -n "$(DEBIAN_DOCKER_CACHE_TO)" ]; then \
	    cache_args="$$cache_args --cache-to type=local,dest=$(DEBIAN_DOCKER_CACHE_TO),mode=max"; \
	  fi; \
	  docker buildx build --load $$cache_args \
	    -f docker/Dockerfile.debian-builder \
	    -t "$(DEBIAN_DOCKER_IMAGE)" .; \
	else \
	  docker buildx build --load \
	    -f docker/Dockerfile.debian-builder \
	    -t "$(DEBIAN_DOCKER_IMAGE)" .; \
	fi
