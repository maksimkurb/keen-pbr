.PHONY: build build-small clean run test

# Build binary
build:
	go build -o keen-pbr ./cmd/keen-pbr

# Build with size optimizations
build-small:
	CGO_ENABLED=0 go build -ldflags="-s -w" -o keen-pbr ./cmd/keen-pbr

# Build for router (MIPS)
build-mips:
	CGO_ENABLED=0 GOOS=linux GOARCH=mipsle go build -ldflags="-s -w" -o keen-pbr-mips ./cmd/keen-pbr

# Build for router (ARM)
build-arm:
	CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 go build -ldflags="-s -w" -o keen-pbr-arm ./cmd/keen-pbr

# Clean build artifacts
clean:
	rm -f keen-pbr keen-pbr-mips keen-pbr-arm

# Run the application
run:
	go run ./cmd/keen-pbr -config config.json

# Run tests
test:
	go test -v ./...

# Format code
fmt:
	go fmt ./...

# Run linter
lint:
	golangci-lint run

# Install dependencies
deps:
	go mod download
	go mod tidy
