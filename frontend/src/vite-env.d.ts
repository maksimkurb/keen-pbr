/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** Target platform baked in at build time; see src/lib/platform.ts. */
  readonly VITE_KEEN_PBR_PLATFORM: "generic" | "openwrt" | "keenetic" | "development"
}

interface ImportMeta {
  readonly env: ImportMetaEnv
}
