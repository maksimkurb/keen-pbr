# keen-pbr frontend

The Web UI uses React, TypeScript, Vite, and Base UI. Install dependencies and
start the local development server from this directory:

```bash
bun install
bun run dev
```

Run the project checks and production build with:

```bash
bun run typecheck
bun run lint
bun run build
```

Use the root Makefile to build assets for a package target:

```bash
make frontend-build FRONTEND_PLATFORM=generic
make frontend-build FRONTEND_PLATFORM=openwrt
make frontend-build FRONTEND_PLATFORM=keenetic
```

Reusable Base UI components are in `src/components/ui`. Follow the existing
component patterns there when adding UI controls.

## Target platform

The bundle is built for one platform: `VITE_KEEN_PBR_PLATFORM` is `generic`
(Debian), `openwrt`, `keenetic`, or `development` (default from `.env`, every
platform feature visible). Release builds pass it to
`build_scripts/build-frontend.sh` (`make frontend-build FRONTEND_PLATFORM=...`).

Gate platform-specific UI with literal comparisons that list the platforms
having the feature plus `PLATFORM_DEVELOPMENT` (see `src/lib/platform.ts`), so
the bundler drops the code of the other platforms:

```ts
import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_GENERIC ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_OPENWRT ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT
```

To develop against a specific router: `VITE_KEEN_PBR_PLATFORM=keenetic bun run dev`.
