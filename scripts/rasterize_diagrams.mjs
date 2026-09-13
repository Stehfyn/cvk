// Rasterize the overview SVG artwork directly, without a viewport or screenshots.
import { readFile, writeFile } from "node:fs/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import { join } from "node:path";

const root = fileURLToPath(new URL("../", import.meta.url));
const modulePath = join(root, "build/diagram-tools/node_modules/@resvg/resvg-js/index.js");
const { Resvg } = await import(pathToFileURL(modulePath).href);
const directory = process.argv[2] || join(root, "study/diagrams");
const names = process.argv.slice(3);
for (const name of (names.length ? names : ["tier-tree", "wrapper-calls"])) {
    const svg = (await readFile(join(directory, name + ".svg"), "utf8"))
        .replace(/<script\b[\s\S]*?<\/script>/g, "");
    const viewBox = svg.match(/viewBox="([^"]+)"/)[1].trim().split(/\s+/).map(Number);
    const renderer = new Resvg(svg, {
        fitTo: { mode: "width", value: Math.ceil(viewBox[2] * 4) },
        font: { loadSystemFonts: true, defaultFontFamily: "Segoe UI" },
    });
    const rendered = renderer.render();
    const target = join(directory, name + "-4x.png");
    await writeFile(target, rendered.asPng());
    console.log(`${target}: ${rendered.width} x ${rendered.height}`);
}
