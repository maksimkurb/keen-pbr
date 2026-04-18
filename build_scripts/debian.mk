## Debian packaging #############################################################
#
# Variables (all overridable via environment or CLI):
#   DEBIAN_DOCKER_IMAGE    — Builder image
#   DEBIAN_VERSION         — Debian distribution name for build/packages layout
#   DEBIAN_DOCKER_CACHE_FROM / DEBIAN_DOCKER_CACHE_TO — Optional buildx cache dirs

DEBIAN_DOCKER_IMAGE ?= keen-pbr-debian-builder:latest
DEBIAN_VERSION ?= bookworm
DEBIAN_DOCKER_CACHE_FROM ?=
DEBIAN_DOCKER_CACHE_TO   ?=

.PHONY: deb-packages debian-builder-image

deb-packages: ## Build Debian packages inside Docker container
	@echo "[deb-packages] config: DEBIAN_VERSION=$(DEBIAN_VERSION) DEBIAN_DOCKER_IMAGE=$(DEBIAN_DOCKER_IMAGE)"
	@if ! docker image inspect "$(DEBIAN_DOCKER_IMAGE)" >/dev/null 2>&1; then \
	  $(MAKE) debian-builder-image; \
	fi
	mkdir -p build/packages
	docker run --rm \
	  -e KEEN_PBR_RELEASE_OVERRIDE="$(KEEN_PBR_RELEASE)" \
	  -v "$(abspath .):/workspace" \
	  "$(DEBIAN_DOCKER_IMAGE)" \
	  bash -c 'set -e; \
	    DEBIAN_VERSION="$(DEBIAN_VERSION)" \
	    bash /workspace/build_scripts/build-debian-packages.sh /workspace /workspace/build/packages'

debian-builder-image: ## Build the Debian builder Docker image locally
	@echo "[debian-builder-image] config: DEBIAN_VERSION=$(DEBIAN_VERSION) DEBIAN_DOCKER_IMAGE=$(DEBIAN_DOCKER_IMAGE) DEBIAN_DOCKER_CACHE_FROM=$(DEBIAN_DOCKER_CACHE_FROM) DEBIAN_DOCKER_CACHE_TO=$(DEBIAN_DOCKER_CACHE_TO)"
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
	  docker build \
	    -f docker/Dockerfile.debian-builder \
	    -t "$(DEBIAN_DOCKER_IMAGE)" .; \
	fi
