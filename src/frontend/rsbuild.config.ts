import { defineConfig } from '@rsbuild/core';
import { pluginReact } from '@rsbuild/plugin-react';
import tailwindcss from '@tailwindcss/postcss';
import path from 'path';

// Docs: https://rsbuild.rs/config/
export default defineConfig({
  plugins: [pluginReact()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, '.'),
    },
  },
  server: {
    proxy: {
      '/api': 'http://192.168.54.1:12121',
    },
  },
  html: {
    meta: {
      viewport: 'width=device-width, initial-scale=1.0, maximum-scale=5.0',
    },
    title: 'keen-pbr',
  },
  tools: {
    postcss: (config, { addPlugins }) => {
      addPlugins([tailwindcss]);
      return config;
    },
  },
});
