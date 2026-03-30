version_mk_dir := $(dir $(lastword $(MAKEFILE_LIST)))
keen_pbr_repo_version_mk := $(abspath $(version_mk_dir)../../../version.mk)
keen_pbr_has_project_version := $(shell [ -f "$(keen_pbr_repo_version_mk)" ] && grep -q '^KEEN_PBR_VERSION[[:space:]]*=' "$(keen_pbr_repo_version_mk)" && echo yes)

ifeq ($(keen_pbr_has_project_version),yes)
include $(keen_pbr_repo_version_mk)
endif

KEEN_PBR_VERSION ?= 3.0.0
KEEN_PBR_RELEASE ?= 1
