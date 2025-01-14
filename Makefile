PKG_VERSION:=1.3.0
PKG_RELEASE:=1
PKG_FULLVERSION:=$(PKG_VERSION)-$(PKG_RELEASE)

BINARY_NAME=keenetic-pbr

build:
	GOARCH=amd64 GOOS=linux go build -ldflags "-s -w" -o bin/${BINARY_NAME}-linux-amd64 main.go
	echo "keenetic-pbr built with size $$(du -BK ./bin/${BINARY_NAME}-linux-amd64 | awk '{print $$1}')"

run: build
	./bin/${BINARY_NAME}

clean:
	go clean
	rm ./bin/${BINARY_NAME}-linux-amd64

test:
	go test ./...

test_coverage:
	go test ./... -coverprofile=coverage.outg

dep:
	go mod download

vet:
	go vet

lint:
	golangci-lint run --enable-all
