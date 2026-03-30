keen_pbr_repo_version_mk := $(abspath $(KEEN_PBR_SRC)/version.mk)

ifeq ($(wildcard $(keen_pbr_repo_version_mk)),)
$(error Missing KEEN_PBR project version file: $(keen_pbr_repo_version_mk))
endif

include $(keen_pbr_repo_version_mk)

ifeq ($(strip $(KEEN_PBR_VERSION)),)
$(error KEEN_PBR_VERSION is empty in $(keen_pbr_repo_version_mk))
endif

ifeq ($(strip $(KEEN_PBR_RELEASE)),)
$(error KEEN_PBR_RELEASE is empty in $(keen_pbr_repo_version_mk))
endif
