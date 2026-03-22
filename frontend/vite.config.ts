import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import viteCompression from "vite-plugin-compression"
import { constants } from "zlib"
import { defineConfig } from "vite"

const textAssetPattern = /\.(html?|css|js|mjs|cjs|jsx|ts|tsx|json|svg|txt|xml|wasm|map)$/i

// https://vite.dev/config/
export default defineConfig(({ mode }) => ({
  plugins: [
    react(),
    tailwindcss(),
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
  },
  server: {
    proxy: {
      "/api": {
        target: "http://192.168.55.1:12121",
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
