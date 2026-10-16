"""Member and parameter descriptions from the Vulkan specification sources (Vulkan-Docs).

Each reference page lists its structure members or command parameters as bullets:

      * pname:indexStride is the byte stride between triangle indices.

load() returns {(page, name): "The byte stride between triangle indices."}, taking the
first sentence of each bullet with the asciidoc markup removed, and {(page, ""): ...} for the
page's own one-line description. Conditional text is resolved for the selected API.
"""

from __future__ import annotations

import os
import re
from typing import Dict, List, Tuple

BULLET = re.compile(r"^  \* (?:\[\[[^\]]+\]\] )?pname:(\w+)((?:,? (?:and |or )?pname:\w+)*)(?:,? (.*))?$")
MACRO = re.compile(r"\b(?:pname|sname|slink|elink|ename|flink|fname|tname|tlink|dname|dlink|basetype|"
                   r"code|reflink|apiext|ptext|etext|stext|ftext|attr|tag|extlink|uri):")
NORMATIVE = re.compile(r"\b(must|should|may|can|cannot|optional|optionally|required|undefined|"
                       r"shall|indexterm)::?")
# Pointers back into the surrounding prose mean nothing next to a single parameter.
CROSS_REFERENCE = re.compile(r",? (?:as |which is |and is |is )?(?:described|explained|defined|specified)"
                             r"(?: in more detail| further| in detail)? (?:below|above)(?: for \w+)?"
                             r"|,? see below| \(see [^)]*\)|,? as described in [^,;.]+")
# Section titles by anchor, so <<anchor>> reads as the title asciidoc renders.
ANCHORS: Dict[str, str] = {}


def anchor_text(anchor: str) -> str:
    """The title asciidoc renders for <<anchor>>; a reference without one (SCID-4) is dropped."""
    return ANCHORS.get(anchor, "")


def clean(text: str, items: Tuple[str, ...] = ()) -> str:
    text = re.sub(r"(?:link:)?https?://[^\s\[]+\[([^\]]*)\]", r"\1", text)
    text = re.sub(r"<<[^,>]+,\s*([^>]+)>>", r"\1", text)
    text = re.sub(r"<<([^>]+)>>", lambda m: anchor_text(m.group(1)), text)
    text = MACRO.sub("", text)
    text = NORMATIVE.sub(r"\1", text)
    text = text.replace("{nbsp}", " ").replace("{ldquo}", "\"").replace("{rdquo}", "\"")
    text = text.replace("{lsquo}", "'").replace("{rsquo}", "'").replace("{times}", "x")
    text = text.replace("{YCbCr}", "Y'CbCr").replace("{RGBprime}", "R'G'B'")
    text = re.sub(r"\{(\w+)\}", r"\1", text)
    text = re.sub(r"\[eq\]#([^#]*)#", r"\1", text)
    text = re.sub(r"~([^~]+)~", r"_\1", text)
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"(?<!\w)[_*]([^_*]+)[_*](?!\w)", r"\1", text)
    text = re.sub(r"\s+", " ", text).strip()
    text = re.sub(r"\s+([,;:.])", r"\1", text)
    optional = re.match(r"if it is not 0, is (.+?)\.\s+Otherwise, the implementation", text)
    if optional:
        text = optional.group(1) + ", or 0 to let the implementation choose."
    sentence = re.split(r"(?<!e\.g)(?<!i\.e)(?<!etc)\.(?:\s|$)", text, maxsplit=1)[0].rstrip(".")
    sentence = CROSS_REFERENCE.sub("", sentence).strip()
    sentence = re.sub(r"^(?:member )?(?:is|are) ", "", sentence)
    if items and (sentence.endswith(":") or "the following" in sentence):
        sentence = sentence.rstrip(":") + ": " + "; ".join(clean(item).rstrip(".") for item in items)
    if re.fullmatch(r"(?:0 and (?:is )?)?reserved for future use", sentence):
        return "Reserved for future use; must be 0."
    return sentence[:1].upper() + sentence[1:] + "."


def defined(names: str, sc: bool) -> bool:
    """An asciidoc condition: A,B is any; A+B is all. Every API attribute is defined except
    the other API's version and the refpage-only switch."""
    def one(name: str) -> bool:
        if name == "isrefpage":
            return False
        if name.startswith("VKSC_VERSION"):
            return sc
        return True
    if "+" in names:
        return all(one(n) for n in names.split("+"))
    return any(one(n) for n in names.split(","))


def resolve(lines: List[str], sc: bool) -> List[str]:
    """Keep only the text the selected API's build of the specification shows."""
    active: List[bool] = []
    out: List[str] = []
    for line in lines:
        block = re.match(r"^(ifdef|ifndef)::([^\[]+)\[\]\s*$", line)
        inline = re.match(r"^(ifdef|ifndef)::([^\[]+)\[(.+)\]\s*$", line)
        if block:
            present = defined(block.group(2), sc)
            active.append(present if block.group(1) == "ifdef" else not present)
        elif re.match(r"^endif::[^\[]*\[\]\s*$", line):
            if active:
                active.pop()
        elif all(active):
            if inline:
                present = defined(inline.group(2), sc)
                if present == (inline.group(1) == "ifdef"):
                    # Inline text continues the surrounding bullet when it sits inside one.
                    inside = out and out[-1].startswith(("  * ", "    ", "  ** "))
                    out.append(("    " if inside else "") + inline.group(3))
            else:
                out.append(line)
    return out


def load(root: str, sc: bool = False) -> Dict[Tuple[str, str], str]:
    sources = []
    for directory in ("chapters", "appendices"):
        for dirpath, _, files in os.walk(os.path.join(root, directory)):
            for file in sorted(files):
                if file.endswith(".adoc"):
                    with open(os.path.join(dirpath, file), encoding="utf-8") as stream:
                        sources.append(resolve(stream.read().splitlines(), sc))
    ANCHORS.clear()
    for lines in sources:
        for anchor, heading in zip(lines, lines[1:]):
            m, title = re.match(r"^\[\[([\w-]+)(?:,\s*([^\]]+))?\]\]$", anchor), re.match(r"^=+ (.+)$", heading)
            if m and (m.group(2) or title):
                ANCHORS[m.group(1)] = (m.group(2) or title.group(1)).strip()
    docs: Dict[Tuple[str, str], str] = {}
    for lines in sources:
        parse(lines, docs)
    complete_type_phrases(docs)
    return docs


def complete_type_phrases(docs: Dict[Tuple[str, str], str]) -> None:
    """A description that only names a type ("A bitmask of VkFooFlagBits") takes the rest of its
    meaning from that type's reference page ("Bitmask specifying ...")."""
    for key, desc in list(docs.items()):
        m = re.fullmatch(r"(An? (?:bitmask of |pointer to (?:an array of )?an? ?)(Vk\w+)( structures?)?)\.", desc)
        if not m or not key[1]:
            continue
        page = docs.get((m.group(2), ""), "")
        phrase = re.match(r"(?:Bitmask|Structure|Structures) ((?:specifying|describing|containing|indicating|controlling) .+)", page)
        if phrase:
            joiner = ", each " if m.group(1).endswith("structures") else " "
            docs[key] = m.group(1) + joiner + phrase.group(1) + "."


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
    names: List[str] = []
    text: List[str] = []
    items: List[str] = []

    def flush() -> None:
        # A bullet naming several members describes them together; each needs its own text.
        if page and len(names) == 1:
            docs.setdefault((page, names[0]), clean(" ".join(text), tuple(items)))
        names.clear()
        text.clear()
        items.clear()

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
        elif names and line.startswith("  ** "):
            items.append(line[5:].strip())
        elif names and items and line.startswith("     ") and line.strip():
            items[-1] += " " + line.strip()
        elif names and not items and line.startswith("    ") and line.strip():
            text.append(line.strip())
        elif not line.strip() or not line.startswith(("//",)):
            flush()
