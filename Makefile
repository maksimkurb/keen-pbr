SHELL := /bin/bash
VERSION := $(shell cat VERSION)
ROOT_DIR := /opt

include repository.mk
include packages.mk

.DEFAULT_GOAL := packages

test:
	go test ./...

build-frontend:
	cd src/frontend && bun install && bun run build

build: build-frontend
	go build -o keen-pbr ./src/cmd/keen-pbr

clean:
	rm -rf out/
	rm -rf src/frontend/dist
	rm -rf src/frontend/node_modules