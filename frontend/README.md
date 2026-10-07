# React + TypeScript + Vite + shadcn/ui

This is a template for a new Vite project with React, TypeScript, and shadcn/ui.

## Adding components

To add components to your app, run the following command:

```bash
npx shadcn@latest add button
```

This will place the ui components in the `src/components` directory.

## Using components

To use the components in your app, import them as follows:

```tsx
import { Button } from "@/components/ui/button"
```

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
