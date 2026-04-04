## OpenWrt packaging ############################################################
#
# Variables (all overridable via environment or CLI):
#   OPENWRT_VERSION        — OpenWrt release version, e.g. 24.10.4
#   OPENWRT_TARGET         — OpenWrt target,    e.g. mediatek
#   OPENWRT_SUBTARGET      — OpenWrt subtarget, e.g. filogic
#   OPENWRT_SDK_DIR        — SDK path inside the container (default: /opt/openwrt-sdk)
#   OPENWRT_SDK_CACHE_DIR  — Host-side directory for SDK caching
#                            Local default: ~/.cache/keen-pbr/openwrt-sdk
#                            CI: set to $RUNNER_TEMP/openwrt-sdk
#   OPENWRT_DOCKER_IMAGE   — Builder image
#   OPENWRT_DOCKER_CACHE_FROM / OPENWRT_DOCKER_CACHE_TO — Optional buildx cache dirs

OPENWRT_VERSION       ?= 24.10.4
OPENWRT_TARGET        ?= mediatek
OPENWRT_SUBTARGET     ?= filogic
OPENWRT_SDK_DIR       ?= /opt/openwrt-sdk
OPENWRT_SDK_CACHE_DIR ?= $(HOME)/.cache/keen-pbr/openwrt-sdk
OPENWRT_DOCKER_IMAGE  ?= keen-pbr-openwrt-builder:latest
OPENWRT_DOCKER_CACHE_FROM ?=
OPENWRT_DOCKER_CACHE_TO   ?=

_OPENWRT_SDK_INSTANCE := sdk-$(OPENWRT_VERSION)-$(OPENWRT_TARGET)-$(OPENWRT_SUBTARGET)

.PHONY: openwrt-sdk-prepare openwrt-packages openwrt-builder-image list-openwrt-targets

openwrt-sdk-prepare: ## Download and prepare the OpenWrt SDK inside Docker (SDK cached on host)
	@echo "[openwrt-sdk-prepare] config: OPENWRT_VERSION=$(OPENWRT_VERSION) OPENWRT_TARGET=$(OPENWRT_TARGET) OPENWRT_SUBTARGET=$(OPENWRT_SUBTARGET) OPENWRT_DOCKER_IMAGE=$(OPENWRT_DOCKER_IMAGE) OPENWRT_SDK_CACHE_DIR=$(OPENWRT_SDK_CACHE_DIR)"
	@if ! docker image inspect "$(OPENWRT_DOCKER_IMAGE)" >/dev/null 2>&1; then \
	  $(MAKE) openwrt-builder-image; \
	fi
	mkdir -p "$(OPENWRT_SDK_CACHE_DIR)/$(_OPENWRT_SDK_INSTANCE)"
	docker run --rm \
	  -v "$(abspath .):/workspace" \
	  -v "$(OPENWRT_SDK_CACHE_DIR)/$(_OPENWRT_SDK_INSTANCE):$(OPENWRT_SDK_DIR)" \
	  "$(OPENWRT_DOCKER_IMAGE)" \
	  bash -c 'set -e; \
	    umask 022; \
	    bash /workspace/build_scripts/openwrt-sdk-setup.sh \
	      "$(OPENWRT_VERSION)" "$(OPENWRT_TARGET)" "$(OPENWRT_SUBTARGET)" \
	      "$(OPENWRT_SDK_DIR)"; \
	    chmod -R u+rwX,go+rX "$(OPENWRT_SDK_DIR)"'

openwrt-packages: openwrt-sdk-prepare ## Build OpenWrt packages inside Docker container (SDK cached on host)
	@echo "[openwrt-packages] config: OPENWRT_VERSION=$(OPENWRT_VERSION) OPENWRT_TARGET=$(OPENWRT_TARGET) OPENWRT_SUBTARGET=$(OPENWRT_SUBTARGET) OPENWRT_DOCKER_IMAGE=$(OPENWRT_DOCKER_IMAGE) OPENWRT_SDK_CACHE_DIR=$(OPENWRT_SDK_CACHE_DIR)"
	mkdir -p "$(OPENWRT_SDK_CACHE_DIR)/$(_OPENWRT_SDK_INSTANCE)" build/packages
	docker run --rm \
	  -e OPENWRT_USIGN_PRIVATE_KEY \
	  -e OPENWRT_APK_PRIVATE_KEY \
	  -v "$(abspath .):/workspace" \
	  -v "$(OPENWRT_SDK_CACHE_DIR)/$(_OPENWRT_SDK_INSTANCE):$(OPENWRT_SDK_DIR)" \
	  "$(OPENWRT_DOCKER_IMAGE)" \
	  bash -c 'set -e; \
	    _SDK=$$(find "$(OPENWRT_SDK_DIR)" -maxdepth 1 -name "openwrt-sdk-*" -type d | head -1); \
	    test -n "$$_SDK" || { echo "ERROR: OpenWrt SDK not found in $(OPENWRT_SDK_DIR)"; exit 1; }; \
	    bash /workspace/build_scripts/build-openwrt-package.sh /workspace "$$_SDK"; \
	    mkdir -p /workspace/build/packages; \
	    bash /workspace/build_scripts/collect-openwrt.sh \
	      /workspace "$$_SDK" /workspace/build/packages \
	      "$(OPENWRT_VERSION)" "$(OPENWRT_TARGET)" "$(OPENWRT_SUBTARGET)"'

openwrt-builder-image: ## Build the OpenWrt builder Docker image locally
	@echo "[openwrt-builder-image] config: OPENWRT_DOCKER_IMAGE=$(OPENWRT_DOCKER_IMAGE) OPENWRT_DOCKER_CACHE_FROM=$(OPENWRT_DOCKER_CACHE_FROM) OPENWRT_DOCKER_CACHE_TO=$(OPENWRT_DOCKER_CACHE_TO)"
	@if [ -n "$(OPENWRT_DOCKER_CACHE_FROM)" ] || [ -n "$(OPENWRT_DOCKER_CACHE_TO)" ]; then \
	  cache_args=""; \
	  if [ -n "$(OPENWRT_DOCKER_CACHE_FROM)" ]; then \
	    cache_args="$$cache_args --cache-from type=local,src=$(OPENWRT_DOCKER_CACHE_FROM)"; \
	  fi; \
	  if [ -n "$(OPENWRT_DOCKER_CACHE_TO)" ]; then \
	    cache_args="$$cache_args --cache-to type=local,dest=$(OPENWRT_DOCKER_CACHE_TO),mode=max"; \
	  fi; \
	  docker buildx build --load $$cache_args \
	    -f docker/Dockerfile.openwrt-builder \
	    -t "$(OPENWRT_DOCKER_IMAGE)" .; \
	else \
	  docker build \
	    -f docker/Dockerfile.openwrt-builder \
	    -t "$(OPENWRT_DOCKER_IMAGE)" .; \
	fi

list-openwrt-targets: ## List available OpenWrt targets from the workflow discovery script
	@python3 "$(abspath .)/docker/list_openwrt_targets.py" "$(OPENWRT_VERSION)" "$(OPENWRT_TARGET)" "$(OPENWRT_SUBTARGET)" "$(abspath .)"
