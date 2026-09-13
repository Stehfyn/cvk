# Repository diagrams

- [What is in each tier? (SVG)](tier-contents.svg) /
  [high-resolution PNG](tier-contents-4x.png): function categories, their roles,
  and real examples across the entire repository. This explains the contents
  rather than just counting functions or drawing their calls.
  The poster groups specializations inside Tier 1 and shows shared support in
  a separate sidebar available across all tiers.
- [Tier tree (high-resolution PNG)](tier-tree-4x.png) and
  [wrapper call overview (high-resolution PNG)](wrapper-calls-4x.png): rendered
  directly from the SVG artwork at 4x its coordinate dimensions, without a
  fixed-size screenshot or surrounding whitespace.
- [Tier tree](tier-tree.svg): both profiles, every numbered tier, support groups,
  platform counts, and the repository's generation/build/research structure.
- [Wrapper call overview](wrapper-calls.svg): every call relationship aggregated
  by profile and tier. Hover arrows for counts and click groups for details.
- [Complete tier tree](tier-functions.svg): every repository C function, grouped
  by profile and tier, with platform labels and source/provenance tooltips.
- [Complete wrapper call graph](wrapper-calls-full.svg): every function and every
  extracted reference, including dependency endpoints. This SVG is intentionally
  large; open it in a browser, zoom, and use Find to locate function names.
- [Wrapper call trees (SVG)](wrapper-call-tree.svg): calls unfolded as branches;
  repeated and cyclic callees link to their first occurrence. Every graph node
  and relationship is retained.
- [Tier tree (text)](tier-tree.txt) and [wrapper call trees (text)](wrapper-call-tree.txt):
  complete trees suitable for an editor or terminal, without a diagram viewer.
- [Graph data](repository-graph.json): source paths, lines, signatures, generator
  origins, and typed edges for the complete diagrams.

All SVGs flow top-down and retain their full vector geometry without a page or
raster-size limit. Open them directly in a browser for **Search**, **Next**,
**Previous**, zoom, and drag-to-pan controls. Search jumps to the matching function
at a readable scale. Repeated-call links jump to their original branches. SVG
viewers that disable scripts still display the complete static vector drawing.

Regenerate from the repository root with Graphviz's `dot` on PATH:

```sh
python scripts/diagram_repository.py
```

Without a native Graphviz installation, the generator uses a local Node.js/WASM
renderer. Install its pinned dependency once:

```sh
npm install --prefix build/diagram-tools --no-audit --no-fund @viz-js/viz@3.30.0
python scripts/diagram_repository.py
```

The matching `.dot` files are also included for alternate Graphviz layouts or
editing. The larger SVGs contain thousands of nodes and are intentionally large.

To regenerate the high-resolution PNGs after generating the SVGs:

```sh
npm install --prefix build/diagram-tools --no-audit --no-fund @resvg/resvg-js@2.6.2
node scripts/rasterize_diagrams.mjs
```

Regenerate the semantic tier-content poster and its high-resolution raster:

```sh
python scripts/diagram_tier_contents.py
node scripts/rasterize_diagrams.mjs study/diagrams tier-contents
```

The function inventory is checked against both generator profiles. All generated
C files under `src/`, including every platform directory, and C files under
`tests/` are scanned. Authored protocol functions appear at their generated
locations, once per supported profile. Header declarations are not duplicate
functions. Python generation/study tools and CMake appear in the repository tree;
they are not runtime wrapper calls. Vendor implementations remain dependency
boundaries.

Classification uses generator provenance and policy steps: single-step chains
with argument bindings are tier 1'; other chains are tier 2. JSON parsers and
fixed helpers have separate support groups. This is a classification, not a
claim that every call descends one tier. The proposed `cvkPlatform*` abstraction
is not emitted and has no function nodes.

Edges distinguish direct calls, explicit Vulkan `ProcAddr` resolutions,
conditional diagnostic macros, function references, and indirect callbacks.
Counts describe static relationships, not runtime execution frequency. Runtime
callback targets cannot be resolved statically. Source and edge details are
available as SVG tooltips and in the JSON.
