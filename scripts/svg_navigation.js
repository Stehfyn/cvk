// Embedded in complete SVGs. The underlying artwork retains its native size.
(() => {
    const svg = document.documentElement;
    const ns = "http://www.w3.org/2000/svg";
    const nativeBox = svg.getAttribute("viewBox").split(/\s+/).map(Number);
    const drawing = document.createElementNS(ns, "g");
    drawing.id = "diagram-artwork";
    for (const child of [...svg.children]) {
        if (child.tagName === "g") drawing.appendChild(child);
    }
    svg.appendChild(drawing);
    const nodes = [...drawing.querySelectorAll("g.node")];
    let scale = 1, x = 0, y = 0, matches = [], matchIndex = 0, drag = null, moved = false;
    const bar = document.createElementNS(ns, "g");
    bar.dataset.ui = "true";
    svg.appendChild(bar);
    const make = (name, attrs, text) => {
        const element = document.createElementNS(ns, name);
        for (const [key, value] of Object.entries(attrs)) element.setAttribute(key, value);
        if (text) element.textContent = text;
        bar.appendChild(element);
        return element;
    };
    const background = make("rect", {x:0, y:0, height:48, fill:"#172033"});
    const button = (left, label, action) => {
        const element = make("text", {x:left, y:29, fill:"white", "font-family":"Segoe UI,Arial,sans-serif", "font-size":14, cursor:"pointer", role:"button", tabindex:0}, label);
        element.addEventListener("click", action);
        element.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") action(); });
    };
    const status = make("text", {x:430, y:29, fill:"#cbd5e1", "font-family":"Segoe UI,Arial,sans-serif", "font-size":12});
    const paint = () => drawing.setAttribute("transform", `translate(${x},${y}) scale(${scale})`);
    const viewport = () => {
        svg.setAttribute("width", "100%");
        svg.setAttribute("height", "100%");
        svg.setAttribute("viewBox", `0 0 ${innerWidth} ${innerHeight}`);
        svg.style.cssText = "width:100vw;height:100vh;touch-action:none;user-select:none;background:#f8fafc";
        background.setAttribute("width", innerWidth);
    };
    const fit = () => {
        scale = Math.min((innerWidth - 32) / nativeBox[2], (innerHeight - 80) / nativeBox[3]);
        x = (innerWidth - nativeBox[2] * scale) / 2;
        y = 60 + (innerHeight - 72 - nativeBox[3] * scale) / 2;
        status.textContent = `${nodes.length.toLocaleString()} nodes | Drag to pan; wheel to zoom; Search to jump`;
        paint();
    };
    const zoom = (factor, cx = innerWidth / 2, cy = innerHeight / 2) => {
        const next = Math.min(12, Math.max(1e-7, scale * factor));
        x = cx - (cx - x) * next / scale;
        y = cy - (cy - y) * next / scale;
        scale = next;
        paint();
    };
    const focus = node => {
        // Use native vector coordinates: screen bounds lose precision when a
        // million-unit drawing is initially fitted into a browser window.
        const geometry = node.querySelector("path,polygon,rect,ellipse") || node;
        const box = geometry.getBBox();
        const matrix = drawing.getCTM().inverse().multiply(geometry.getCTM());
        const center = new DOMPoint(box.x + box.width / 2, box.y + box.height / 2).matrixTransform(matrix);
        const cx = center.x, cy = center.y;
        scale = Math.min(2, (innerWidth - 100) / (box.width + 100));
        x = innerWidth / 2 - cx * scale;
        y = (innerHeight + 48) / 2 - cy * scale;
        paint();
    };
    const find = query => {
        const lowered = query.toLowerCase();
        matches = nodes.filter(node => [...node.querySelectorAll("text")].some(text => text.textContent.toLowerCase().includes(lowered)));
        matchIndex = 0;
        if (matches.length) focus(matches[0]);
        status.textContent = matches.length ? `1 / ${matches.length} matches: ${query}` : `No matches: ${query}`;
        return matches.length;
    };
    const next = () => {
        if (!matches.length) return;
        matchIndex = (matchIndex + 1) % matches.length;
        focus(matches[matchIndex]);
        status.textContent = `${matchIndex + 1} / ${matches.length} matches`;
    };
    const search = () => { const query = prompt("Function or group name:"); if (query) find(query); };
    button(18, "Fit", fit);
    button(64, "+", () => zoom(2));
    button(95, "-", () => zoom(.5));
    button(133, "Search /", search);
    button(226, "Next", next);
    button(287, "Previous", () => { if (matches.length) { matchIndex = (matchIndex + matches.length - 2) % matches.length; next(); } });
    svg.addEventListener("wheel", e => { e.preventDefault(); zoom(Math.exp(-e.deltaY * .004), e.clientX, e.clientY); }, {passive:false});
    svg.addEventListener("pointerdown", e => {
        if (e.target.closest("[data-ui]")) return;
        drag = {x:e.clientX, y:e.clientY, startX:x, startY:y}; moved = false;
        svg.setPointerCapture(e.pointerId);
    });
    svg.addEventListener("pointermove", e => {
        if (!drag) return;
        moved ||= Math.hypot(e.clientX - drag.x, e.clientY - drag.y) > 4;
        x = drag.startX + e.clientX - drag.x; y = drag.startY + e.clientY - drag.y; paint();
    });
    svg.addEventListener("pointerup", () => { drag = null; });
    svg.addEventListener("click", e => {
        if (moved) { e.preventDefault(); moved = false; return; }
        const link = e.target.closest("a");
        const href = link?.getAttribute("href") || link?.getAttributeNS("http://www.w3.org/1999/xlink", "href");
        if (href?.startsWith("#")) {
            const target = document.getElementById(href.slice(1));
            if (target) { e.preventDefault(); focus(target); }
        }
    }, true);
    addEventListener("keydown", e => {
        if (e.key === "/") { e.preventDefault(); search(); }
        if (e.key === "Escape") fit();
    });
    addEventListener("resize", () => { viewport(); fit(); });
    viewport(); fit();
    if (location.hash) {
        const target = document.getElementById(decodeURIComponent(location.hash.slice(1)));
        if (target) focus(target);
    }
    window.cvkDiagram = {find, next, fit, nodeCount:nodes.length};
})();
