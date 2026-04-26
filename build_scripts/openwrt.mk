## OpenWrt packaging ############################################################
#
# Variables (all overridable via environment or CLI):
#   OPENWRT_VERSION        — OpenWrt release version, e.g. 24.10.4
#   OPENWRT_ARCHITECTURE   — OpenWrt package architecture, e.g. aarch64_cortex-a53
#   OPENWRT_SDK_DIR        — Preferred SDK root inside the container (default: /)
#   OPENWRT_DOCKER_IMAGE   — Official OpenWrt SDK image

OPENWRT_VERSION       ?=
OPENWRT_ARCHITECTURE  ?=
OPENWRT_SDK_DIR       ?= /
OPENWRT_DOCKER_IMAGE  ?= ghcr.io/openwrt/sdk:$(OPENWRT_ARCHITECTURE)-$(OPENWRT_VERSION)

define _require_nonempty
$(if $(strip $($1)),,$(error $1 is required for target '$2'))
endef

.PHONY: openwrt-packages openwrt-sign-packages list-openwrt-architectures

openwrt-packages: ## Build OpenWrt packages inside the official OpenWrt SDK container
	$(call _require_nonempty,OPENWRT_VERSION,$@)
	$(call _require_nonempty,OPENWRT_ARCHITECTURE,$@)
	@echo "[openwrt-packages] config: OPENWRT_VERSION=$(OPENWRT_VERSION) OPENWRT_ARCHITECTURE=$(OPENWRT_ARCHITECTURE) OPENWRT_DOCKER_IMAGE=$(OPENWRT_DOCKER_IMAGE)"
	mkdir -p build build/packages/openwrt build/packages/openwrt-debug
	chmod 0777 build build/packages build/packages/openwrt build/packages/openwrt-debug
	docker run --rm \
	  -e OPENWRT_VERSION="$(OPENWRT_VERSION)" \
	  -e OPENWRT_ARCHITECTURE="$(OPENWRT_ARCHITECTURE)" \
	  -e OPENWRT_USIGN_PRIVATE_KEY \
	  -e OPENWRT_APK_PRIVATE_KEY \
	  -e KEEN_PBR_RELEASE_OVERRIDE="$(KEEN_PBR_RELEASE)" \
	  -e HOME=/tmp/keen-pbr-home \
	  -v "$(abspath .):/workspace" \
	  "$(OPENWRT_DOCKER_IMAGE)" \
	  bash -lc 'set -e; \
	    mkdir -p "$$HOME"; \
	    _SDK=$$(bash /workspace/build_scripts/find-openwrt-sdk.sh "$(OPENWRT_SDK_DIR)"); \
	    bash /workspace/build_scripts/build-openwrt-package.sh /workspace "$$_SDK"; \
	    mkdir -p /workspace/build/packages; \
	    bash /workspace/build_scripts/collect-openwrt.sh \
	      /workspace "$$_SDK" /workspace/build/packages \
	      "$(OPENWRT_VERSION)" "$(OPENWRT_ARCHITECTURE)"'

openwrt-sign-packages: ## Sign OpenWrt repository metadata inside the official OpenWrt SDK container
	$(call _require_nonempty,OPENWRT_VERSION,$@)
	$(call _require_nonempty,OPENWRT_ARCHITECTURE,$@)
	@echo "[openwrt-sign-packages] config: OPENWRT_VERSION=$(OPENWRT_VERSION) OPENWRT_ARCHITECTURE=$(OPENWRT_ARCHITECTURE) OPENWRT_DOCKER_IMAGE=$(OPENWRT_DOCKER_IMAGE)"
	mkdir -p build build/packages/openwrt
	chmod 0777 build build/packages build/packages/openwrt
	docker run --rm \
	  -e OPENWRT_USIGN_PRIVATE_KEY \
	  -e OPENWRT_APK_PRIVATE_KEY \
	  -e HOME=/tmp/keen-pbr-home \
	  -v "$(abspath .):/workspace" \
	  "$(OPENWRT_DOCKER_IMAGE)" \
	  bash -lc 'set -e; \
	    mkdir -p "$$HOME"; \
	    _SDK=$$(bash /workspace/build_scripts/find-openwrt-sdk.sh "$(OPENWRT_SDK_DIR)"); \
	    bash /workspace/build_scripts/sign-openwrt-repository.sh \
	      "$$_SDK" /workspace/build/packages "$(OPENWRT_VERSION)"'

list-openwrt-architectures: ## List available OpenWrt package architectures for a release
	$(call _require_nonempty,OPENWRT_VERSION,$@)
	@python3 "$(abspath .)/.github/builder/generate_openwrt_arch_matrix.py" "$(OPENWRT_VERSION)" --format table
