import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { resolve } from "node:path";

export default defineConfig({
  plugins: [react()],
  server: { host: "0.0.0.0" },
  build: {
    rollupOptions: {
      input: {
        main: resolve(__dirname, "index.html"),
        legacy: resolve(__dirname, "legacy/index.html"),
      },
    },
  },
});
