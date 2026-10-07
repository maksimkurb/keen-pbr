/**
 * Target platforms of a frontend bundle. The platform is baked in at build
 * time through `import.meta.env.VITE_KEEN_PBR_PLATFORM`; release builds set it
 * per package, the dev server uses `development` (default in `.env`).
 *
 * Compare against these constants directly, list the platforms that HAVE a
 * feature and always include PLATFORM_DEVELOPMENT, e.g.
 *   import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_GENERIC ||
 *   import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_OPENWRT ||
 *   import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT
 * The bundler then sees literal comparisons and drops the code of the other
 * platforms (keep plain `===`/`||`: `[...].includes()` is not folded), and a
 * new platform never gets a feature by accident.
 */
export const PLATFORM_GENERIC = "generic"
export const PLATFORM_OPENWRT = "openwrt"
export const PLATFORM_KEENETIC = "keenetic"
export const PLATFORM_DEVELOPMENT = "development"
