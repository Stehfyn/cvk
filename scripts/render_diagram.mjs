// Render unconstrained top-down DOT as SVG with the local Graphviz WASM tool.
import { readFile, writeFile } from "node:fs/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import { join } from "node:path";
import { isMainThread, Worker } from "node:worker_threads";

if (isMainThread) {
    // Graphviz traverses the complete repository graph recursively. Give its
    // worker enough native stack for thousands of nodes on Windows as well.
    const worker = new Worker(new URL(import.meta.url), {
        argv: process.argv.slice(2),
        resourceLimits: { stackSizeMb: 64 },
    });
    await new Promise((resolve, reject) => {
        worker.on("error", reject);
        worker.on("exit", code => code ? reject(new Error(`Graphviz exited ${code}`)) : resolve());
    });
} else {
    const root = fileURLToPath(new URL("../", import.meta.url));
    const modulePath = join(root, "build/diagram-tools/node_modules/@viz-js/viz/dist/viz.js");
    const { instance } = await import(pathToFileURL(modulePath).href);
    const viz = await instance();
    const [input, output] = process.argv.slice(2);
    let svg = viz.renderString(await readFile(input, "utf8"), { format: "svg", engine: "dot" });
    const navigation = await readFile(join(root, "scripts/svg_navigation.js"), "utf8");
    svg = svg.replace("</svg>", `<script type="application/ecmascript"><![CDATA[\n${navigation}\n]]></script>\n</svg>`);
    await writeFile(output, svg);
    console.log(`Rendered ${output}`);
}
