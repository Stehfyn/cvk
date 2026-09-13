"""Explain the actual contents of every repository tier as an SVG poster.

    python scripts/diagram_repository.py
    python scripts/diagram_tier_contents.py
    node scripts/rasterize_diagrams.mjs study/diagrams tier-contents
"""

from collections import Counter
import html
import json
from pathlib import Path
import textwrap


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "study/diagrams"

# These are semantic kinds, with concrete examples checked against the complete
# graph inventory. They explain the tiers; they are not name-prefix classifiers.
SECTIONS = [
    ("3", "Tier 3 / domain protocols", "Authored C algorithms and workflows folded into the generated libraries.", "#a855f7", [
        ("External resource interchange", "Import/export images and buffers; handle ownership and platform capability checks.", ["cvkImportWin32HandleImage", "cvkImportDmaBufImage", "cvkExportFdBuffer"]),
        ("Memory, video and ray tracing", "Mapped-memory I/O, video-session allocation, acceleration structures and shader binding tables.", ["cvkWriteMemory", "cvkAllocateVideoSessionMemory", "cvkSetupRayTracingShaderBindingTable"]),
        ("Transfers and format arithmetic", "Copy/blit with ownership transitions; compute plane layouts and copy regions.", ["cvkCmdTransferBufferToImageQueueOwnership", "cvkCalculateFormatPlaneLayouts", "cvkGetBufferImageCopyRegions"]),
        ("JSON document machinery", "Token/value parsing, string and member access, arenas and runtime handle resolution.", ["cvkParseJsonDocument", "cvkAllocateArena", "cvkResolveJsonHandle"]),
        ("Pipeline JSON workflows", "Parse object graphs, resolve their handles and create the described Vulkan pipeline.", ["cvkParsePipelineJson", "cvkCreatePipelineJsonObjects", "cvkCreatePipelineFromJson"]),
        ("Debugging and device identity", "Create a debug-enabled instance, report messages and find devices by LUID or PCI address.", ["cvkCreateDebugInstance", "cvkWriteDebugUtilsMessage", "cvkGetPhysicalDeviceByPciBusInfo"]),
    ]),
    ("2", "Tier 2 / compositions", "Policy chains combine multiple calls, scratch structures, queries and selected results.", "#2563eb", [
        ("Resource setup and allocation", "Create resources, inspect requirements, select memory, allocate it and bind it.", ["cvkSetupBuffer", "cvkSetupImage", "cvkAllocateBufferMemory"]),
        ("Devices, queues and capabilities", "Query/select a device capability or memory type; build a device and retrieve its queue.", ["cvkSetupDeviceQueue", "cvkGetPhysicalDeviceVideoProfileSupport", "cvkGetPhysicalDeviceCompatibleMemoryTypeIndex"]),
        ("Pipelines and descriptors", "Build graphics/compute/mesh/ray-tracing pipelines, layouts and descriptor updates.", ["cvkSetupGraphicsPipeline", "cvkSetupRayTracingPipeline", "cvkUpdateImageDescriptorSet"]),
        ("Recording sequences", "Bind required state and issue graphics, compute, mesh, video or ray-tracing work.", ["cvkCmdDispatchComputeItems", "cvkCmdDecodeVideoPicture", "cvkCmdTraceRayTracingRays"]),
        ("Barriers and ownership", "Build barrier structures and submit them, including queue-family ownership transfers.", ["cvkCmdMemoryBarrier2", "cvkCmdImageQueueOwnershipBarrier2", "cvkCmdBufferQueueOwnershipBarrier"]),
        ("Compound builders and adapters", "Build nested structures, union members or 2D geometry before forwarding to another helper.", ["cvkCreateImage2D", "cvkInitTrianglesAccelerationStructureGeometry", "cvkCmdSetFirstViewportAndScissor2D"]),
    ]),
    ("1p", "Tier 1' / specializations", "A singular operation or a one-call policy chain fixes arguments of a more general helper.", "#0891b2", [
        ("Count-of-one operations", "Turn an array operation into one element, including per-element extension variants.", ["cvkCreateComputePipeline", "cvkWaitForFence", "cvkBindPlaneImageMemory"]),
        ("Resource and memory presets", "Choose fixed flags or properties, such as device-local memory or a signaled fence.", ["cvkSetupDeviceLocalBuffer", "cvkAllocateHostVisibleBufferMemory", "cvkCreateSignaledFence"]),
        ("Pipeline-state presets", "Bind standard rasterization, blending, input-assembly and other fixed-function choices.", ["cvkInitFillPipelineRasterizationStateCreateInfo", "cvkInitAlphaBlendPipelineColorBlendAttachmentState", "cvkInitTriangleListPipelineInputAssemblyStateCreateInfo"]),
        ("Synchronization presets", "Fix queue families, stage/access choices, timeout values or a timestamp stage.", ["cvkCmdAcquireImageFromExternal", "cvkCmdReleaseBufferToExternal2", "cvkAcquireNextImageIndefinitely"]),
        ("Operation and stage variants", "Choose a pipeline bind point, shader stage, video control flag or acceleration-structure kind.", ["cvkCmdBindComputePipeline", "cvkCmdResetVideoCoding", "cvkCreateBottomLevelAccelerationStructure"]),
        ("Shape and dispatch shortcuts", "Bind a dimension, item-count expression or transfer-policy argument in one forwarding call.", ["cvkCmdTraceRays2D", "cvkCmdDispatchItems", "cvkCmdTransferBufferToImage"]),
    ]),
    ("1", "Tier 1 / builders and extension conveniences", "Construct Vulkan data or add a registry-defined extension to an existing operation.", "#059669", [
        ("Struct and union builders", "Fill sType, pNext and members of input structures, plain structures and union alternatives.", ["cvkInitBufferCreateInfo", "cvkInitImageSubresourceRange", "cvkInitClearValueColor"]),
        ("Input extension chains", "Build one extender and prepend it to the caller's pNext chain before the base operation.", ["cvkCreateBufferWithExternalMemoryInfo", "cvkAllocateExportMemory", "cvkCreateImageViewWithUsageInfo"]),
        ("Output extension queries", "Attach an output property/requirement structure and return that extension's result.", ["cvkGetBufferMemoryDedicatedRequirements", "cvkGetPhysicalDeviceIDProperties", "cvkGetPhysicalDevicePCIBusInfoProperties"]),
        ("Per-element input extensions", "Build and attach an extension for each input array element, then call the array operation.", ["cvkBindPlaneImageMemories", "cvkBindSwapchainImageMemories", "cvkCmdBindUsageFlags2DescriptorBuffers"]),
        ("Per-element output extensions", "Extend every output-array element in a query or fixed-capacity enumeration.", ["cvkGetPhysicalDeviceQueueFamilyCheckpointPropertiesNV", "cvkGetDisplayModeStereoPropertiesNV"]),
    ]),
    ("0", "Tier 0 / command wrappers", "The registry-facing layer calls Vulkan, handles results and preserves the wrapper's output contracts.", "#ea580c", [
        ("Object and memory lifecycle", "Creation/destruction, allocation/freeing, binding and host memory operations.", ["cvkCreateBuffer", "cvkAllocateMemory", "cvkBindBufferMemory"]),
        ("Command recording", "Draw, dispatch, copy, rendering, acceleration-structure, video and other vkCmd operations.", ["cvkCmdDraw", "cvkCmdDispatch", "cvkCmdCopyBuffer"]),
        ("Queries and flattened siblings", "Retrieve properties and requirements; flatten input Info structs where a sibling is required.", ["cvkGetPhysicalDeviceProperties", "cvkGetBufferMemoryRequirements", "cvkGetBufferMemoryRequirementsInfo2"]),
        ("Fixed-capacity enumeration", "Perform Vulkan's count/fill protocol using caller arrays bounded by CVK_MAX_* capacities.", ["cvkEnumeratePhysicalDevices", "cvkEnumerateDeviceExtensionProperties", "cvkGetPhysicalDeviceSurfaceFormats"]),
        ("Submission and synchronization", "Submit queues, acquire/present images, wait/reset fences and signal/wait semaphores.", ["cvkQueueSubmit", "cvkQueuePresent", "cvkWaitForFences"]),
        ("Dispatch and platform commands", "Resolve extension commands or wrap platform-specific Vulkan calls in their platform files.", ["cvkGetDeviceProcAddr", "cvkCreateWin32Surface", "cvkCreateXlibSurface"]),
    ]),
    ("support", "Support groups / used throughout the tiers", "These groups have explicit roles but are not extra numbered abstraction levels.", "#64748b", [
        ("Generated JSON scalar parsers", "Parse enum names, flag sets and scalar values into their Vulkan representations.", ["cvkParseJsonImageLayout", "cvkParseJsonBufferUsageFlags"]),
        ("Generated JSON structure parsers", "Populate profile-supported Vulkan structs and arrays from the parsed JSON document.", ["cvkParseJsonBufferCreateInfo", "cvkParseJsonGraphicsPipelineCreateInfo"]),
        ("Generated JSON pNext dispatch", "Choose and populate the correct extension structure while traversing a JSON chain.", ["cvkParseJsonStructureChain"]),
        ("Pure selection algorithms", "Choose memory types, queue families and supported extensions from already queried data.", ["cvkGetCompatibleMemoryTypeIndex", "cvkGetQueueFamilyIndex", "cvkGetExtensionsSupport"]),
        ("Arithmetic and local state", "Align sizes and choose an unused video-reference slot without calling Vulkan.", ["cvkAlignDeviceSize", "cvkGetInactiveVideoReferenceSlotIndex"]),
        ("Fatal diagnostics", "Report failed checks and abort. CHECK and ASSERT macros reach this infrastructure.", ["cvkCheckResult", "cvkFatal"]),
    ]),
]


def main():
    data = json.loads((OUT / "repository-graph.json").read_text(encoding="utf-8"))
    nodes = {n["name"]: n for n in data["nodes"] if "file" in n}
    counts = Counter((n["profile"], n["tier"]) for n in nodes.values())
    for key, _, _, _, groups in SECTIONS:
        for _, _, examples in groups:
            for name in examples:
                assert name in nodes, f"Missing example: {name}"
                assert nodes[name]["tier"] in ({"json", "util"} if key == "support" else {key}), f"Wrong tier: {name}"

    sections = {section[0]: section for section in SECTIONS}
    width, row_height = 4100, 358
    tier_one_height = 688
    footer = 180 + 3 * row_height + tier_one_height
    height = footer + 165
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img">',
             '<title>What is in each cvk tier?</title>',
             '<desc>Whole-repository semantic tier guide with verified examples from both Vulkan profiles, all platforms, JSON and helper support.</desc>',
             f'<rect width="{width}" height="{height}" fill="#f8fafc"/>',
             '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#172033}.example{font-family:Consolas,monospace;fill:#334155}.muted{fill:#64748b}</style>',
             '<defs><marker id="down" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L10 5L0 10Z" fill="#94a3b8"/></marker></defs>']

    def text(x, y, value, size=18, css="", weight=400):
        parts.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" class="{css}">{html.escape(value)}</text>')

    text(44, 65, "What is in each tier?", 42, weight=700)
    text(44, 104, "A semantic map of the whole repository: Vulkan + Vulkan SC, core + platform code, generated + authored functions.", 23)
    text(44, 139, "Each card names a kind of work, describes its role, and gives actual functions. The SC tag means the corresponding scvk function is also emitted.", 20, "muted")
    lines = ["WHAT IS IN EACH TIER?", ""]

    def card(left, y, card_width, kind, detail, examples, color):
        parts.append(f'<g class="content-card"><rect x="{left}" y="{y}" width="{card_width}" height="225" rx="8" fill="white" stroke="#cbd5e1"/><rect x="{left}" y="{y}" width="6" height="225" rx="3" fill="{color}"/>')
        text(left + 18, y + 31, kind, 23, weight=600)
        for line_index, line in enumerate(textwrap.wrap(detail, int((card_width - 36) / 9.6))):
            text(left + 18, y + 65 + line_index * 24, line, 19)
        text(left + 18, y + 144, "Actual examples", 15, "muted")
        for e_index, example in enumerate(examples):
            sc = " [SC]" if "s" + example in nodes else ""
            label = example + sc
            size = min(17, (card_width - 36) / (len(label) * .61))
            parts.append(f'<title>{html.escape(nodes[example]["file"] + ":" + str(nodes[example]["line"]))}</title>')
            text(left + 18, y + 167 + e_index * 23, label, size, "example")
        parts.append('</g>')
        lines.extend(["  " + kind + ": " + detail] + ["    " + name for name in examples])

    def card_row(groups, y, color, parent_bottom=None):
        gap = 18
        card_width = (3272 - gap * (len(groups) - 1)) / len(groups)
        for g_index, (kind, detail, examples) in enumerate(groups):
            left = 44 + g_index * (card_width + gap)
            center = left + card_width / 2
            if parent_bottom is not None:
                parts.append(f'<path d="M{center} {parent_bottom}V{y - 6}" stroke="#94a3b8" fill="none" marker-end="url(#down)"/>')
            card(left, y, card_width, kind, detail, examples, color)

    top = 180
    for key in ("0", "1", "2", "3"):
        _, title, description, color, groups = sections[key]
        tier_count = {profile: counts[profile, key] for profile in ("cvk", "scvk")}
        if key == "1":
            title = "Tier 1 / conveniences"
            description = "Build Vulkan data, extend existing calls, or specialize an operation with fixed arguments."
            tier_count = {profile: tier_count[profile] + counts[profile, "1p"] for profile in tier_count}
            parts.append(f'<g id="tier-1"><rect x="32" y="{top - 10}" width="3296" height="668" rx="12" fill="#f0fdf4" stroke="{color}" stroke-width="2"/>')
        parts.append(f'<rect x="44" y="{top}" width="3272" height="68" rx="8" fill="white" stroke="{color}" stroke-width="2"/>')
        text(62, top + 29, title, 26, weight=700)
        text(62, top + 54, description, 19)
        count_label = f'cvk: {tier_count["cvk"]:,} functions / scvk: {tier_count["scvk"]:,}'
        text(2720, top + 29, count_label, 20, "muted")
        lines += [title, description, count_label]
        if key == "1":
            text(62, top + 100, "Builders and extension chains", 23, weight=600)
            lines.append("  Builders and extension chains")
            card_row(groups, top + 115, color)
            text(62, top + 382, "Specializations", 23, weight=600)
            text(268, top + 382, "Count-of-one forms and argument presets within Tier 1", 19, "muted")
            lines.append("  Specializations / within Tier 1")
            card_row(sections["1p"][4], top + 410, color)
            parts.append('</g>')
            top += tier_one_height
        else:
            card_row(groups, top + 103, color, top + 68)
            top += row_height
        lines.append("")

    # Shared support sits beside all numbered tiers, with no tier number or
    # hierarchy arrows. It is available to any layer that needs it.
    sidebar_x, sidebar_width = 3356, 700
    parts.append(f'<g id="shared-support"><rect x="{sidebar_x}" y="180" width="{sidebar_width}" height="{footer - 200}" rx="12" fill="#eef2f6" stroke="#cbd5e1"/>')
    text(sidebar_x + 20, 219, "Shared support", 28, weight=700)
    text(sidebar_x + 20, 252, "Available across tiers", 21)
    count_label = f'JSON parsers: {counts["cvk", "json"] + counts["scvk", "json"]:,} / helpers: {counts["cvk", "util"] + counts["scvk", "util"]:,}'
    text(sidebar_x + 20, 281, count_label, 18, "muted")
    lines += ["SHARED SUPPORT / AVAILABLE ACROSS TIERS", count_label]
    for index, (kind, detail, examples) in enumerate(sections["support"][4]):
        card(sidebar_x + 18, 310 + index * 242, sidebar_width - 36, kind, detail, examples, "#64748b")
    parts.append('</g>')

    text(44, footer + 28, "How to read the boundaries", 24, weight=700)
    text(44, footer + 63, "Tier 1 includes builders, extension conveniences and specializations. cvkSetupDeviceLocalBuffer is a specialization; cvkSetupBuffer is a Tier 2 composition.", 19)
    text(44, footer + 96, "Platform folders contain multiple tiers. JSON document/pipeline workflows are Tier 3; generated type parsers and shared helpers sit beside the tier stack.", 19)
    text(44, footer + 129, "Tests exercise the generated API. Python generation/research tools and CMake are repository tooling; the proposed cvkPlatform* interface is not emitted.", 19, "muted")
    parts.append('</svg>')
    (OUT / "tier-contents.svg").write_text("\n".join(parts) + "\n", encoding="utf-8")
    (OUT / "tier-contents.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Verified {sum(len(examples) for section in SECTIONS for _, _, examples in section[4])} real examples; wrote tier-contents.svg ({width} x {height}).")


if __name__ == "__main__":
    main()
