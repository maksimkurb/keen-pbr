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

install-dev-deps:
	go install honnef.co/go/tools/cmd/staticcheck@latest
	go install mvdan.cc/unparam@latest
	go install golang.org/x/tools/cmd/deadcode@latest

test:
	go vet ./... && go test ./... && staticcheck -checks 'all,-U1000' ./... && unparam ./... && deadcode ./...

build-frontend:
	cd src/frontend && npm install && npm run build

build: build-frontend
	go build -ldflags "$(GO_LDFLAGS) -w -s" -o keen-pbr ./src/cmd/keen-pbr

build-dev: build-frontend
	go build -tags dev -ldflags "$(GO_LDFLAGS)" -o keen-pbr ./src/cmd/keen-pbr

clean:
	rm -rf out/
	rm -rf src/frontend/dist
	rm -rf src/frontend/node_modules
