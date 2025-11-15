.PHONY: build build-small clean run test build-frontend

# Build frontend assets
build-frontend:
	cd ../keen-pbr-ui/ && bun run build
	rm -rf internal/api/dist
	cp -r ../keen-pbr-ui/dist internal/api/dist

# Build binary
build: build-frontend
	go build -o keen-pbr ./cmd/keen-pbr

# Build with size optimizations
build-small: build-frontend
	CGO_ENABLED=0 go build -ldflags="-s -w" -o keen-pbr ./cmd/keen-pbr

# Build for router (MIPS)
build-mips: build-frontend
	CGO_ENABLED=0 GOOS=linux GOARCH=mipsle go build -ldflags="-s -w" -o keen-pbr-mips ./cmd/keen-pbr

# Build and deploy to router (MIPS)
deploy-mips: build-mips
	scp -P 222 keen-pbr-mips root@192.168.54.1:/opt/root/keen-pbr

# Build for router (ARM)
build-arm: build-frontend
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
