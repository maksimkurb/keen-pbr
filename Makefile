SHELL := /bin/bash
VERSION := $(shell cat VERSION)
ROOT_DIR := /opt
COMMIT := $(shell git rev-parse --short HEAD)
DATE := $(shell date -u +%Y-%m-%d)

GO_LDFLAGS := -X 'github.com/maksimkurb/keen-pbr/src/internal/api.Version=$(VERSION)' \
	-X 'github.com/maksimkurb/keen-pbr/src/internal/api.Date=$(DATE)' \
	-X 'github.com/maksimkurb/keen-pbr/src/internal/api.Commit=$(COMMIT)'

include repository.mk
include packages.mk

.DEFAULT_GOAL := packages

test:
	go vet ./... && go test ./... && staticcheck ./...

build-frontend:
	cd src/frontend && npm install && npm run build

build: build-frontend
	go build -ldflags "$(GO_LDFLAGS)" -o keen-pbr ./src/cmd/keen-pbr

clean:
	rm -rf out/
	rm -rf src/frontend/dist
	rm -rf src/frontend/node_modules
