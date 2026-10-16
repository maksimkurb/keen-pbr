import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import viteCompression from "vite-plugin-compression"
import { constants } from "zlib"
import { defineConfig, loadEnv } from "vite"

const textAssetPattern =
  /\.(html?|css|js|mjs|cjs|jsx|ts|tsx|json|svg|txt|xml|wasm|map)$/i

const PLATFORMS = ["generic", "openwrt", "keenetic", "development"]

// The platform is baked into the bundle (src/lib/platform.ts); a missing value
// would silently hide every platform-gated feature, so refuse to start.
function assertPlatform(mode: string) {
  const platform = loadEnv(mode, process.cwd(), "VITE_").VITE_KEEN_PBR_PLATFORM
  if (!PLATFORMS.includes(platform)) {
    throw new Error(
      `VITE_KEEN_PBR_PLATFORM must be one of ${PLATFORMS.join(", ")} (got "${platform ?? ""}"); see frontend/.env`
    )
  }
}

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  assertPlatform(mode)
  return {
    plugins: [
      react(),
      tailwindcss(),
      viteCompression({
        algorithm: "gzip",
        ext: ".gz",
        threshold: 0,
        filter: textAssetPattern,
        deleteOriginFile: true,
        disable: mode === "development",
        compressionOptions: {
          level: constants.Z_BEST_COMPRESSION,
        },
      }),
    ],
    build: {
      outDir: process.env.KEEN_PBR_FRONTEND_OUT_DIR || "dist",
      emptyOutDir: true,
    },
    server: {
      proxy: {
        "/api": {
          target: process.env.ROUTER_URL || "http://192.168.54.1:12121",
          changeOrigin: true,
        },
      },
    },
    resolve: {
      alias: {
        "@": path.resolve(__dirname, "./src"),
      },
    },
  }
})
