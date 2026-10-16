"""Member and parameter descriptions from the Vulkan specification sources (Vulkan-Docs).

Each reference page lists its structure members or command parameters as bullets:

      * pname:indexStride is the byte stride between triangle indices.

load() returns {(page, name): "The byte stride between triangle indices."}, taking the
first sentence of each bullet with the asciidoc markup removed.
"""

from __future__ import annotations

import os
import re
from typing import Dict, Tuple

BULLET = re.compile(r"^  \* (?:\[\[[^\]]+\]\] )?pname:(\w+)((?:,? (?:and |or )?pname:\w+)*)(?:,? (.*))?$")
MACRO = re.compile(r"\b(?:pname|sname|slink|elink|ename|flink|fname|tname|tlink|dname|dlink|basetype|"
                   r"code|reflink|apiext|ptext|etext|stext|ftext|attr|tag|extlink|uri):")
NORMATIVE = re.compile(r"\b(must|should|may|can|cannot|optional|optionally|required|undefined|"
                       r"shall|indexterm)::?")


def clean(text: str) -> str:
    text = re.sub(r"<<[^,>]+,\s*([^>]+)>>", r"\1", text)
    text = re.sub(r"<<([^>]+)>>", r"\1", text)
    text = MACRO.sub("", text)
    text = NORMATIVE.sub(r"\1", text)
    text = text.replace("{nbsp}", " ").replace("{ldquo}", "\"").replace("{rdquo}", "\"")
    text = text.replace("{lsquo}", "'").replace("{rsquo}", "'").replace("{times}", "x")
    text = text.replace("{YCbCr}", "Y'CbCr").replace("{RGBprime}", "R'G'B'")
    text = re.sub(r"\{(\w+)\}", r"\1", text)
    text = re.sub(r"\[eq\]#([^#]*)#", r"\1", text).replace("~", "")
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"(?<!\w)[_*]([^_*]+)[_*](?!\w)", r"\1", text)
    text = re.sub(r"\s+", " ", text).strip()
    optional = re.match(r"if it is not 0, is (.+?)\.\s+Otherwise, the implementation", text)
    if optional:
        text = optional.group(1) + ", or 0 to let the implementation choose."
    sentence = re.split(r"(?<!e\.g)(?<!i\.e)(?<!etc)\.(?:\s|$)", text, maxsplit=1)[0].rstrip(".")
    sentence = re.sub(r"^(?:is|are) ", "", sentence)
    if sentence == "reserved for future use":
        return "Reserved for future use; must be 0."
    return sentence[:1].upper() + sentence[1:] + "."


def load(root: str) -> Dict[Tuple[str, str], str]:
    docs: Dict[Tuple[str, str], str] = {}
    for directory in ("chapters", "appendices"):
        for dirpath, _, files in os.walk(os.path.join(root, directory)):
            for file in sorted(files):
                if file.endswith(".adoc"):
                    with open(os.path.join(dirpath, file), encoding="utf-8") as stream:
                        parse(stream.read().splitlines(), docs)
    return docs


def parse(lines, docs: Dict[Tuple[str, str], str]) -> None:
    # A bullet may carry only its anchor, with the member on the next line (the limits pages).
    joined = []
    for line in lines:
        if joined and re.fullmatch(r"  \* \[\[[^\]]+\]\]\s*", joined[-1]) and line.startswith("    pname:"):
            joined[-1] = "  * " + line.strip()
        else:
            joined.append(line)
    lines = joined
    page = None
    names, text = [], []

    def flush() -> None:
        if page and names:
            for name in names:
                docs.setdefault((page, name), clean(" ".join(text)))
        names.clear()
        text.clear()

    for line in lines:
        opened = re.match(r"^\[open,refpage='(\w+)'(?:,desc='([^']*)')?", line)
        bullet = BULLET.match(line)
        if opened:
            flush()
            page = opened.group(1)
            # The page's own one-line description, under the empty name.
            if opened.group(2) and not opened.group(2).startswith("Stub description"):
                docs.setdefault((page, ""), clean(opened.group(2)).rstrip("."))
        elif bullet:
            flush()
            names.extend([bullet.group(1)] + re.findall(r"pname:(\w+)", bullet.group(2)))
            text.append(bullet.group(3) or "")
        elif names and line.startswith("    ") and line.strip():
            text.append(line.strip())
        elif not line.startswith(("ifdef::", "ifndef::", "endif::")):
            flush()
