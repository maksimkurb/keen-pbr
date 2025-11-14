# keen-pbr

A lightweight policy-based routing utility for routers, written in Go.

## Features

- JSON-based configuration
- RESTful API for management
- Multiple DNS resolver types (UDP, TLS, HTTPS)
- Local and remote IP/domain lists
- Multiple outbound interfaces and proxies
- URL-based outbound selection
- Minimal binary size optimized for routers

## Building

### Standard build
```bash
make build
```

### Size-optimized build (for routers)
```bash
make build-small
```

### Cross-compile for MIPS routers
```bash
make build-mips
```

### Cross-compile for ARM routers
```bash
make build-arm
```

## Running

```bash
./keen-pbr -config /path/to/config.json -listen :8080
```

## API Endpoints

### Service Control
- `POST /v1/service/start` - Start the service
- `POST /v1/service/stop` - Stop the service
- `POST /v1/service/restart` - Restart the service
- `POST /v1/service/enable` - Enable autostart
- `POST /v1/service/disable` - Disable autostart
- `GET /v1/service/status` - Get service status

### Rules Management
- `GET /v1/rules` - List all rules
- `POST /v1/rules` - Create a new rule
- `GET /v1/rules/{id}` - Get a specific rule
- `PUT /v1/rules/{id}` - Update a rule
- `DELETE /v1/rules/{id}` - Delete a rule

### Outbound Tables Management
- `GET /v1/outbound-tables` - List all outbound tables
- `POST /v1/outbound-tables` - Create a new outbound table
- `GET /v1/outbound-tables/{id}` - Get a specific outbound table
- `PUT /v1/outbound-tables/{id}` - Update an outbound table
- `DELETE /v1/outbound-tables/{id}` - Delete an outbound table

### Configuration
- `GET /v1/config` - Get full configuration
- `PUT /v1/config` - Update full configuration

## Configuration Format

See [example-config.json](example-config.json) for a complete configuration example.

## Size Optimization

The binary is optimized for minimal size:
- Uses Go standard library for HTTP (no frameworks)
- Stripped debug symbols with `-ldflags="-s -w"`
- Optional UPX compression
- Typical size: 2-4 MB compressed

## License

MIT
