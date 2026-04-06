## Keenetic packaging ###########################################################
#
# Variables (all overridable via environment or CLI):
#   KEENETIC_CONFIG        — Entware config name, e.g. mipsel-3.4
#   KEENETIC_VERSION       — Keenetic channel version for build/packages layout
#   KEENETIC_DOCKER_IMAGE  — Docker image to use for building (default: derived from config)

KEENETIC_CONFIG       ?=
KEENETIC_VERSION      ?=
KEENETIC_DOCKER_IMAGE ?= ghcr.io/maksimkurb/entware-builder:$(KEENETIC_CONFIG)

define _require_nonempty
$(if $(strip $($1)),,$(error $1 is required for target '$2'))
endef

.PHONY: keenetic-packages

keenetic-packages: ## Build Keenetic packages inside Entware Docker container
	$(call _require_nonempty,KEENETIC_CONFIG,$@)
	$(call _require_nonempty,KEENETIC_VERSION,$@)
	@echo "[keenetic-packages] config: KEENETIC_CONFIG=$(KEENETIC_CONFIG) KEENETIC_VERSION=$(KEENETIC_VERSION) KEENETIC_DOCKER_IMAGE=$(KEENETIC_DOCKER_IMAGE)"
	mkdir -p build/packages
	docker run --rm --user root \
	  --entrypoint /bin/bash \
	  -v "$(abspath .):/workspace" \
	  "$(KEENETIC_DOCKER_IMAGE)" \
	  -lc 'set -e; \
	    sh /workspace/build_scripts/build-keenetic-package.sh /workspace /home/me/Entware; \
	    mkdir -p /workspace/build/packages; \
	    sh /workspace/build_scripts/collect-keenetic.sh \
	      /workspace /home/me/Entware/bin /workspace/build/packages \
	      "$(KEENETIC_CONFIG)" "$(KEENETIC_VERSION)"'
