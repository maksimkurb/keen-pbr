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
	go install github.com/golangci/golangci-lint/v2/cmd/golangci-lint@v2.6.2

test:
	go vet ./... && go test ./... && staticcheck -checks 'all,-U1000' ./... && unparam ./... && deadcode ./... && golangci-lint-v2 run

generate-types:
	go run ./cmd/generate-types

build-frontend: generate-types
	cd src/frontend && bun install && bun run build

build:
	go build -ldflags "$(GO_LDFLAGS) -w -s" -o keen-pbr ./src/cmd/keen-pbr

build-dev:
	go build -tags dev -ldflags "$(GO_LDFLAGS)" -o keen-pbr ./src/cmd/keen-pbr

DEPLOY_IP := 192.168.54.1
DEPLOY_PORT := 222

deploy-mipsel: build-frontend
	GOOS=linux GOARCH=mipsle go build -tags dev -ldflags "$(GO_LDFLAGS) -w -s" -o keen-pbr ./src/cmd/keen-pbr
	scp -P $(DEPLOY_PORT) ./keen-pbr root@$(DEPLOY_IP):/opt/usr/bin/keen-pbr
	scp -r -P $(DEPLOY_PORT) src/frontend/dist/* root@$(DEPLOY_IP):/opt/usr/share/keen-pbr/ui/

clean:
	rm -rf out/
	rm -rf src/frontend/dist
	rm -rf src/frontend/node_modules
