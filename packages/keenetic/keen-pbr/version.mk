version_mk_dir := $(dir $(lastword $(MAKEFILE_LIST)))
include $(version_mk_dir)../../../version.mk
