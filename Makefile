SHELL := /bin/bash
VERSION := $(shell cat VERSION)
ROOT_DIR := /opt

include repository.mk
include packages.mk

.DEFAULT_GOAL := packages

test:
	go test ./...

clean:
	rm -rf out/