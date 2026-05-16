import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import viteCompression from "vite-plugin-compression"
import { constants } from "zlib"
import { defineConfig } from "vite"

import { demoApiPlugin } from "./demo/vite-plugin-demo-api"

const textAssetPattern = /\.(html?|css|js|mjs|cjs|jsx|ts|tsx|json|svg|txt|xml|wasm|map)$/i

// https://vite.dev/config/
const isDemoMode = (mode: string) => mode === "demo"

export default defineConfig(({ mode }) => ({
  plugins: [
    react(),
    tailwindcss(),
    ...(isDemoMode(mode) ? [demoApiPlugin()] : []),
    viteCompression({
      algorithm: "gzip",
      ext: ".gz",
      threshold: 0,
      filter: textAssetPattern,
      deleteOriginFile: false,
      disable: mode === "development",
      compressionOptions: {
        level: constants.Z_BEST_COMPRESSION,
      },
    }),
  ],
  build: {
    outDir: process.env.KEEN_PBR_FRONTEND_OUT_DIR || "dist",
    emptyOutDir: true,
    chunkSizeWarningLimit: 768,
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (!id.includes("node_modules")) {
            return undefined
          }
          if (
            id.includes("@tanstack/react-query") ||
            id.includes("@tanstack/query-core")
          ) {
            return "tanstack-query"
          }
          if (
            id.includes("@tanstack/react-form") ||
            id.includes("@tanstack/form-core") ||
            id.includes("@tanstack/store")
          ) {
            return "tanstack-form"
          }
          if (
            id.includes("i18next") ||
            id.includes("react-i18next") ||
            id.includes("i18next-")
          ) {
            return "i18n"
          }
          if (id.includes("lucide-react")) {
            return "icons"
          }
          return "vendor"
        },
      },
    },
  },
  server: isDemoMode(mode)
    ? {
        host: true,
        port: 5173,
        strictPort: true,
      }
    : {
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
}))
