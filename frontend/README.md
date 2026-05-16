# keen-pbr frontend

React + Vite web UI for the keen-pbr daemon. Package manager: [Bun](https://bun.sh).

## Prerequisites

- Bun 1.2+
- For E2E on Linux CI: Chromium via Playwright (`bun run e2e:install`)

## Scripts

| Command | Description |
|---------|-------------|
| `bun install` | Install dependencies |
| `bun run dev` | Dev server; proxies `/api` to `ROUTER_URL` (default `http://192.168.54.1:12121`) |
| `bun run dev:demo` | Dev server with in-memory mock API (no router required) |
| `bun run build` | `tsc -b` + production bundle to `dist/` |
| `bun run lint` | ESLint |
| `bun run typecheck` | `tsc --noEmit` |
| `bun test ./tests` | Bun unit tests |
| `bun run e2e` | Playwright (builds + `vite preview` on port 4173) |
| `bun run api:generate` | Regenerate Orval client under `src/api/generated/` |
| `bun run api:check` | Regenerate and fail if generated files drift |

## Local development

```sh
cd frontend
bun install
export ROUTER_URL=http://127.0.0.1:12121   # optional
bun run dev
```

Demo mode (mock API, banner in the shell):

```sh
bun run dev:demo
```

## Quality gate (matches CI)

```sh
bun run lint
bun run typecheck
bun test ./tests
bun run build
bun run api:check
bun run e2e
```

Without Bun on the host, use Docker:

```sh
docker run --rm -v "$PWD":/app -w /app oven/bun:1.2 bash -lc "bun install && bun run lint && bun run typecheck && bun test ./tests && bun run build"
```

E2E with Playwright image:

```sh
docker run --rm --ipc=host -v "$PWD":/app -w /app mcr.microsoft.com/playwright:v1.60.0-noble bash -lc \
  'apt-get update -qq && apt-get install -y -qq unzip curl >/dev/null && curl -fsSL https://bun.sh/install | bash && export PATH=$HOME/.bun/bin:$PATH && bun install && bun run e2e'
```

## Generated API types

Do not edit `src/api/generated/` by hand. Update `docs/openapi.yaml` at the repo root, then:

```sh
make frontend-api-generate   # from repository root
```

## Shared mock data

`fixtures/app-mock-config.ts` is used by `demo/` and Playwright `e2e/api-mocks.ts` so offline dev and tests stay aligned.
