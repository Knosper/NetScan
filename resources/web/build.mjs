import {rm, rename} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";
import esbuild from "esbuild";

const rootDir = path.dirname(fileURLToPath(import.meta.url));
const outDir = path.join(rootDir, ".build");

await rm(outDir, {recursive: true, force: true});

await esbuild.build({
    entryPoints: [path.join(rootDir, "src/main.jsx")],
    bundle: true,
    format: "iife",
    target: ["es2020"],
    minify: true,
    sourcemap: false,
    outdir: outDir,
    loader: {
        ".css": "css"
    }
});

await rename(path.join(outDir, "main.js"), path.join(rootDir, "app.js"));
await rename(path.join(outDir, "main.css"), path.join(rootDir, "style.css"));
await rm(outDir, {recursive: true, force: true});
