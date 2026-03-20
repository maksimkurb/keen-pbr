SHELL := bash

RELEASE_DIR ?= release_files
DOCKER_OPENWRT_IMAGE ?= keen-pbr-openwrt-builder
DOCKER_KEENETIC_IMAGE_PREFIX ?= keen-pbr-keenetic-builder
OPENWRT_VERSION ?= 24.10.4
REPO_ROOT ?= $(abspath $(CURDIR))

KEENETIC_CONTAINER = keen-pbr-keenetic-$(KEENETIC_ARCH)
KEENETIC_IMAGE = $(DOCKER_KEENETIC_IMAGE_PREFIX):$(KEENETIC_ARCH)
OPENWRT_CONTAINER = keen-pbr-openwrt-$(OPENWRT_VERSION)-$(OPENWRT_TARGET)-$(OPENWRT_SUBTARGET)

.PHONY: docker-build-keenetic docker-build-openwrt docker-build-openwrt-image docker-build-keenetic-image docker-clean-keenetic docker-clean-openwrt list-openwrt-targets

define require_var
	@test -n "$($1)" || { \
		echo "Error: $1 is not set."; \
		echo "Usage: $2"; \
		exit 1; \
	}
endef

docker-build-keenetic-image:
	$(call require_var,KEENETIC_ARCH,KEENETIC_ARCH=<aarch64-3.10|mips-3.4|mipsel-3.4|x64-3.2|armv7-3.2> make docker-build-keenetic)
	docker build -f docker/Dockerfile.keenetic-builder --build-arg KEENETIC_ARCH="$(KEENETIC_ARCH)" -t "$(KEENETIC_IMAGE)" .

docker-build-openwrt-image:
	docker build -f docker/Dockerfile.openwrt-builder -t "$(DOCKER_OPENWRT_IMAGE)" .

list-openwrt-targets: ## List available OpenWrt targets from the workflow discovery script (optional OPENWRT_VERSION/OPENWRT_TARGET/OPENWRT_SUBTARGET filters)
	@python3 "$(REPO_ROOT)/docker/list_openwrt_targets.py" "$(OPENWRT_VERSION)" "$(OPENWRT_TARGET)" "$(OPENWRT_SUBTARGET)" "$(REPO_ROOT)"

docker-build-keenetic: docker-build-keenetic-image ## Build Keenetic package locally in a persistent builder container (requires KEENETIC_ARCH=...)
	$(call require_var,KEENETIC_ARCH,KEENETIC_ARCH=<aarch64-3.10|mips-3.4|mipsel-3.4|x64-3.2|armv7-3.2> make docker-build-keenetic)
	@mkdir -p "$(RELEASE_DIR)"
	@if ! docker container inspect "$(KEENETIC_CONTAINER)" >/dev/null 2>&1; then \
		echo "[keenetic] Creating persistent container $(KEENETIC_CONTAINER)..."; \
		docker create --name "$(KEENETIC_CONTAINER)" --user root -v "$(REPO_ROOT):/src" "$(KEENETIC_IMAGE)" >/dev/null; \
	fi
	@set -e; \
	trap 'docker stop "$(KEENETIC_CONTAINER)" >/dev/null 2>&1 || true' EXIT; \
	docker start "$(KEENETIC_CONTAINER)" >/dev/null; \
	docker exec -e KEENETIC_ARCH="$(KEENETIC_ARCH)" -e RELEASE_DIR="/src/$(RELEASE_DIR)" -e REPO_ROOT="/src" "$(KEENETIC_CONTAINER)" bash /src/docker/keenetic-build.sh

docker-build-openwrt: docker-build-openwrt-image ## Build OpenWrt package locally in a persistent builder container (requires OPENWRT_TARGET=... OPENWRT_SUBTARGET=...)
	$(call require_var,OPENWRT_TARGET,OPENWRT_TARGET=<target> OPENWRT_SUBTARGET=<subtarget> [OPENWRT_VERSION=$(OPENWRT_VERSION)] make docker-build-openwrt)
	$(call require_var,OPENWRT_SUBTARGET,OPENWRT_TARGET=<target> OPENWRT_SUBTARGET=<subtarget> [OPENWRT_VERSION=$(OPENWRT_VERSION)] make docker-build-openwrt)
	@mkdir -p "$(RELEASE_DIR)"
	@if ! docker container inspect "$(OPENWRT_CONTAINER)" >/dev/null 2>&1; then \
		echo "[openwrt] Creating persistent container $(OPENWRT_CONTAINER)..."; \
		docker create --name "$(OPENWRT_CONTAINER)" -v "$(REPO_ROOT):/src" "$(DOCKER_OPENWRT_IMAGE)" >/dev/null; \
	fi
	@set -e; \
	trap 'docker stop "$(OPENWRT_CONTAINER)" >/dev/null 2>&1 || true' EXIT; \
	docker start "$(OPENWRT_CONTAINER)" >/dev/null; \
	docker exec \
		-e OPENWRT_VERSION="$(OPENWRT_VERSION)" \
		-e OPENWRT_TARGET="$(OPENWRT_TARGET)" \
		-e OPENWRT_SUBTARGET="$(OPENWRT_SUBTARGET)" \
		-e RELEASE_DIR="/src/$(RELEASE_DIR)" \
		-e REPO_ROOT="/src" \
		"$(OPENWRT_CONTAINER)" \
		bash /src/docker/openwrt-build.sh

docker-clean-keenetic: ## Remove persistent Keenetic builder containers/images (optionally scoped with KEENETIC_ARCH=...)
	@if [ -n "$(KEENETIC_ARCH)" ]; then \
		docker rm -f "$(KEENETIC_CONTAINER)" 2>/dev/null || true; \
		docker image rm "$(KEENETIC_IMAGE)" 2>/dev/null || true; \
	else \
		docker ps -a --format '{{.Names}}' | grep '^keen-pbr-keenetic-' | xargs -r docker rm -f; \
		docker images --format '{{.Repository}}:{{.Tag}}' | grep '^$(DOCKER_KEENETIC_IMAGE_PREFIX):' | xargs -r docker image rm; \
	fi

docker-clean-openwrt: ## Remove persistent OpenWrt builder containers (optionally scoped with OPENWRT_VERSION/OPENWRT_TARGET/OPENWRT_SUBTARGET)
	@if [ -n "$(OPENWRT_TARGET)" ] && [ -n "$(OPENWRT_SUBTARGET)" ]; then \
		docker rm -f "$(OPENWRT_CONTAINER)" 2>/dev/null || true; \
	else \
		docker ps -a --format '{{.Names}}' | grep '^keen-pbr-openwrt-' | xargs -r docker rm -f; \
	fi
