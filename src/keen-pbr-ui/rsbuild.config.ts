import { defineConfig } from '@rsbuild/core';
import { pluginReact } from '@rsbuild/plugin-react';

export default defineConfig({
  plugins: [pluginReact()],
  source: {
    entry: {
      index: './src/main.tsx',
    },
  },
  server: {
    port: 3030,
    proxy: {
      '/v1': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
    },
  },
  tools: {
    postcss: {
      postcssOptions: {
        plugins: [
          require('@tailwindcss/postcss'),
          require('autoprefixer'),
        ],
      },
    },
  },
  output: {
    distPath: {
      root: 'dist',
    },
  },
});
