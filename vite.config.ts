import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

const projectRoot = fileURLToPath(new URL(".", import.meta.url));

export default defineConfig({
  plugins: [react()],
  server: { host: "0.0.0.0" },
  build: {
    rollupOptions: {
      input: {
        main: resolve(projectRoot, "index.html"),
        legacy: resolve(projectRoot, "legacy/index.html"),
      },
    },
  },
});
