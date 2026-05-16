# Agent Notes

## Build

Always build using the root `Makefile`:

```sh
make
```

This runs `cmake -S . -B cmake-build ...` followed by `cmake --build cmake-build`.

## Generated Files

Never edit generated files by hand. Update the source schema/config and run the
appropriate codegen command instead.

- Backend API types (`src/api/generated/api_types.hpp`): run `make generate`.
- Frontend API client/models (`frontend/src/api/generated/`): run `make frontend-api-generate`.

## Frontend

Frontend lives in the `frontend/` folder.
Always use bun/bunx as a package manager.
We are using base-ui instead of radix-ui.

### Frontend dev & test

```sh
cd frontend && bun install
bun run dev              # proxy /api to ROUTER_URL
bun run dev:demo         # in-memory mock API (no daemon)
bun run lint && bun run typecheck && bun test ./tests && bun run build
bun run e2e              # Playwright (see frontend/README.md)
bun run api:check        # Orval drift check
```

Mock config for demo and E2E: `frontend/fixtures/app-mock-config.ts`.
