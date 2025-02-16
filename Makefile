SHELL := /bin/bash
VERSION := $(shell cat VERSION)
ROOT_DIR := /opt

include repository.mk
include packages.mk

.DEFAULT_GOAL := packages

clean:
	rm -rf out/