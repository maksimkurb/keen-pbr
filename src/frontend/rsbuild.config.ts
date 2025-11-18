import { defineConfig } from '@rsbuild/core';
import { pluginReact } from '@rsbuild/plugin-react';
import tailwindcss from '@tailwindcss/postcss';

// Docs: https://rsbuild.rs/config/
export default defineConfig({
  plugins: [pluginReact()],
  tools: {
    postcss: (config, { addPlugins }) => {
      addPlugins([
        tailwindcss
      ]);
      return config;
    }
  }
});
