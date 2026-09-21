import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";
import { compression } from "vite-plugin-compression2";
import path from "path";
import { mockPlugin } from "./mock/plugin.ts";

// Every route the SPA uses. It is deliberately short: PROMPT.md §6.4 forbids the
// UI from having a path the same API does not expose to everyone else.
const proxyRoutes = ["/api", "/ws"];  // every path the firmware serves, and no other

// Post-build: replace type="module" with plain script tag for IIFE compatibility
function fixScriptType() {
  return {
    name: "fix-script-type",
    enforce: "post" as const,
    generateBundle(_: unknown, bundle: Record<string, { type: string; source?: string }>) {
      for (const file of Object.values(bundle)) {
        if (file.type === "asset" && typeof file.source === "string" && file.source.includes("type=\"module\"")) {
          file.source = file.source
            .replace(' type="module" crossorigin', " defer")
            .replace(' crossorigin', "");
        }
      }
    },
  };
}

export default defineConfig(({ mode }) => {
  // Device IP the dev server proxies /api and /ws to — set VITE_DEVICE_IP in web/.env.local.
  // loadEnv is REQUIRED: Vite does not copy .env files into process.env at config-eval time,
  // so reading process.env.VITE_DEVICE_IP alone silently ignored .env.local and fell back to
  // the default. The last arg "" loads all vars, not only the VITE_-prefixed client ones.
  const env = loadEnv(mode, process.cwd(), "");
  const deviceIP = env.VITE_DEVICE_IP || "192.168.50.92";
  const target = `http://${deviceIP}`;
  const mocking = mode === "mock";

  return {
  plugins: [
    preact(),
    fixScriptType(),
    compression({
      algorithms: ["gzip"],
      include: /\.(js|css|html)$/,
      deleteOriginalAssets: true,
    }),
    ...(mocking ? [mockPlugin()] : []),
  ],
  build: {
    // Consumed by the firmware build through EMBED_FILES; see main/CMakeLists.txt.
    outDir: path.resolve(__dirname, "dist"),
    emptyOutDir: true,
    target: "es2018",
    modulePreload: false,
    cssCodeSplit: false,
    assetsInlineLimit: 4096,
    rollupOptions: {
      output: {
        manualChunks: undefined,
        format: "iife",
        entryFileNames: "app.js",
        assetFileNames: "[name][extname]",
      },
    },
  },
  server: mocking
    ? {} // the mock plugin serves /api and /ws in-process; no proxy to a device
    : {
        // `/ws` MUST enable the WebSocket upgrade (`ws: true`) or the push socket that carries the
        // live device state never connects and the dashboard sits on "Connecting…". changeOrigin
        // rewrites the Host header so the firmware sees a request for itself, not for the dev server.
        proxy: Object.fromEntries(
          proxyRoutes.map((r) => [r, { target, changeOrigin: true, ws: r === "/ws" }]),
        ),
      },
  };
});
