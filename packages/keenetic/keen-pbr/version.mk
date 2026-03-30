version_mk_dir := $(dir $(lastword $(MAKEFILE_LIST)))
keen_pbr_repo_version_mk := $(abspath $(version_mk_dir)../../../version.mk)
keen_pbr_has_project_version := $(shell [ -f "$(keen_pbr_repo_version_mk)" ] && grep -q '^KEEN_PBR_VERSION[[:space:]]*=' "$(keen_pbr_repo_version_mk)" && echo yes)

ifeq ($(wildcard $(keen_pbr_repo_version_mk)),)
$(error Missing KEEN_PBR project version file: $(keen_pbr_repo_version_mk))
endif

ifneq ($(keen_pbr_has_project_version),yes)
$(error KEEN_PBR_VERSION is not defined in $(keen_pbr_repo_version_mk))
endif

include $(keen_pbr_repo_version_mk)

ifeq ($(strip $(KEEN_PBR_VERSION)),)
$(error KEEN_PBR_VERSION is empty in $(keen_pbr_repo_version_mk))
endif

ifeq ($(strip $(KEEN_PBR_RELEASE)),)
$(error KEEN_PBR_RELEASE is empty in $(keen_pbr_repo_version_mk))
endif
