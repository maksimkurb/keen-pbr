import { defineConfig } from "orval"

export default defineConfig({
  keenApi: {
    input: "../docs/openapi.yaml",
    output: {
      target: "./src/api/generated/keen-api.ts",
      schemas: "./src/api/generated/model",
      client: "react-query",
      mode: "split",
      override: {
        mutator: {
          path: "./src/api/client.ts",
          name: "apiFetch",
        },
      },
    },
  },
})
