"""Report CastDemo convention violations and lifetime boundaries without a baseline allowlist.

This is a lexical audit, not a C parser, MISRA analyzer, allocation trace, or proof of
realtime behavior. Every C/header file is scanned. Findings retain file/line evidence.
Manual requirements remain open even when all lexical checks pass.
"""

import argparse
import collections
import hashlib
import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples" / "CastDemo"
NON_CODE = re.compile(r'/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
COMMENTS_AND_STRINGS = re.compile(r'/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|(?P<character>(?:u8|[uUL])?\'(?:\\.|[^\'\\])*\')', re.S)
RULES = {
    "crt-warning-suppression": r"(?m)^[ \t]*#[ \t]*define[ \t]+_CRT_(?:SECURE_NO_WARNINGS|NONSTDC_NO_WARNINGS|NONSTDC_NO_DEPRECATE|SECURE_NO_DEPRECATE)\b",
    "no-inline-barrier": r"\b(?:NO_INLINE|NOINLINE|noinline)\b",
    "os-public-prefix": r"\bOs_\w*\b",
    "crt-public-prefix": r"\b(?!CRT_)(?i:crt_)\w*\b",
    "generic-win32-api": r"\b(?:CreateWindowEx|RegisterClassEx|DefWindowProc|GetMessage|PeekMessage|DispatchMessage|LoadIcon|LoadCursor|CreateFont|CreateFontIndirect|GetWindowText|SetWindowText|SendMessage|PostMessage|GetModuleHandle|FindFirstFile|FindNextFile|CreateFile|DeleteFile|GetTempPath|GetTempFileName|FormatMessage|GetOpenFileName|GetSaveFileName|ShellExecute|CreateEvent|OutputDebugString|GetEnvironmentVariable|GetCommandLine|SetWindowLongPtr|GetWindowLongPtr|IsDialogMessage|MessageBox)[AW]\s*\(",
    "do-while-loop": r"\bdo\b",
    "while-loop": r"\bwhile\b",
    "explicit-width-types": r"\bsize_t\b",
    "unsigned-literal-suffix": r"\b(?:0[xX][0-9a-fA-F]+|\d+)[uU](?:[lL]{0,2})\b",
    "boolean-constant-comparison": r"\b(?:TRUE|FALSE)\s*(?:==|!=)|(?:==|!=)\s*(?:TRUE|FALSE)\b",
    "dereference-sizeof": r"\bsizeof\s*\(\s*\*",
    "array-count-macro": r"\b_countof\s*\(",
    "declaration-in-for": r"\bfor\s*\(\s*(?:const\s+)?(?:int|unsigned|long|DWORD|uint\d+_t|bool_t|size_t)\b",
    "crt-call": r"\b(?:malloc|calloc|realloc|free|memcpy|memmove|memset|memcmp|memchr|strlen|strnlen|strcmp|strncmp|strstr|strchr|strrchr|strcspn|strspn|strpbrk|strcpy|strncpy|strcat|strncat|wcslen|wcscmp|wcsncmp|wcschr|wcsrchr|wcscpy|wcsncpy|wcsstr|snprintf|vsnprintf|sprintf|sscanf|swscanf|printf|fprintf|puts|fopen|_wfopen|fclose|fread|fwrite|fseek|ftell|_fseeki64|_ftelli64|atoi|atof|strtol|strtoul|strtoull|strtod|wcstoul|sin|cos|sqrt|floor|ceil|fmod|isfinite|_snwprintf_s|_snprintf_s|_vsnprintf_s|_wcsicmp|_stricmp)\s*\(",
}
BOUNDARIES = {
    "allocation-and-release": r"\b(?:malloc|calloc|realloc|free|HeapAlloc|HeapReAlloc|HeapFree|VirtualAlloc|VirtualFree|OS_Memory_Initialize|OS_Memory_TryClose|OS_Network_Initialize|Ssdp_Initialize|Ssdp_TryClose|CastSession_Initialize|CastSession_TryClose|MediaServer_Initialize|MediaServer_TryClose|Proxy_Initialize|Proxy_TryClose|LocalAlloc|LocalFree|GlobalAlloc|GlobalFree|CoTaskMemAlloc|CoTaskMemFree|MapViewOfFile|UnmapViewOfFile|CreateFileMappingW)\s*\(",
    "thread-and-resource-lifetime": r"\b(?:CreateThread|OS_Thread_Initialize|OS_Thread_TryClose|OS_Console_Initialize|OS_Console_TryClose|OS_TemporaryFile_Initialize|OS_TemporaryFile_TryClose|GetTempFileName[AW]?|DeleteFile[AW]?|CreateEvent[AW]?|CreateSemaphore[AW]?|CreateFile[AW]?|CreateSolidBrush|CreateFont[AW]?|CreateFontIndirect[AW]?|DestroyWindow|DuplicateHandle|CloseHandle|DeleteObject|InitializeSecurityContext[AW]?|AcquireCredentialsHandle[AW]?|FreeContextBuffer|WinHttpOpen|WinHttpConnect|WinHttpOpenRequest|WinHttpCloseHandle|socket|WSASocket[AW]?|accept|closesocket|bind|GetAdaptersAddresses|getaddrinfo|freeaddrinfo|MFCreate\w*|vkCreate\w*|vkAllocate\w*|cvkCreate\w*|cvkAllocate\w*)\s*\(",
    "waiting-and-io": r"\b(?:WaitForSingleObject|WaitForMultipleObjects|Sleep|SleepEx|AcquireSRWLockExclusive|EnterCriticalSection|OS_Console_ReadKey|ReadConsoleInput(?:Ex)?[AW]?|WriteConsoleInput[AW]?|SetConsoleMode|ReadFile|WriteFile|SetFilePointerEx|recv|send|recvfrom|sendto|connect|accept|select|WSAIoctl|ioctlsocket|WinHttpSendRequest|WinHttpReceiveResponse|WinHttpReadData|WinHttpQueryHeaders|IMFSourceReader_ReadSample|IMFSinkWriter_WriteSample|vkWaitForFences|cvkWaitForFences)\s*\(",
}
COMMON_SECTIONS = [
    "1. References", "2. INCLUDE FILES", "3. DECLARATIONS", "3.1 Macros", "3.2 Types"
]
SOURCE_SECTIONS = COMMON_SECTIONS + [
    "3.3 External global variables", "3.4 Static global variables",
    "3.5 Static function prototypes", "4. PRIVATE FUNCTIONS", "5. PUBLIC FUNCTIONS",
]
HEADER_SECTIONS = COMMON_SECTIONS + ["3.3 Constants", "3.4 Variables", "3.5 Functions"]
FUNCTION_SEPARATOR = "/*************************************************************************************************/"
PUBLIC_DECLARATION = re.compile(
    r"^(?!typedef\b)[\w \t*]+?\b((?:[A-Z][A-Za-z0-9]*_)+[A-Za-z0-9]+)\s*\([^;{}]*\);",
    re.M,
)
PUBLIC_OPERATION = re.compile(
    r"^(?:Initialize|Get|Is|Has|Can|Needs|Create|Destroy|Open|Close|Start|Stop|Begin|End|"
    r"Reset|Prepare|Set|Read|Write|Find|Derive|Resolve|Inspect|Encode|Decode|Parse|Format|Compare|"
    r"Copy|Move|Load|Store|Exchange|Fetch|Peek|Replace|Remove|Push|Pop|Take|Try|Publish|"
    r"Consume|Produce|Connect|Send|Receive|Run|Post|Apply|Convert|Probe|Seek|Flush|Finish|"
    r"Add|Mark|Snap|Discover|Describe|Skip|Submit|Assert|Exit|Main)(?:[A-Z0-9]|$)"
)


def blank_non_code(match):
    return re.sub(r"[^\r\n]", " ", match.group())


def packed_argument_offsets(source):
    """Find shared start lines in wrapped argument/parameter lists, preserving literals.

    This checks lexical layout, not C types or macro expansion. Nested argument
    lists and commas inside strings or compound expressions are independent.
    """
    def mask(match):
        blank = blank_non_code(match)
        return blank if match[0].startswith(("/*", "//")) else "X" + blank[1:]

    code = NON_CODE.sub(mask, source)
    stack = []
    excluded = {"if", "for", "while", "switch", "sizeof", "_Alignof"}
    for token in re.finditer(r"[A-Za-z_]\w*|[^\s]", code):
        value = token[0]
        if value == "(":
            prefix = re.search(r"\b([A-Za-z_]\w*)\s*$", code[:token.start()])
            is_call = prefix is not None and prefix[1] not in excluded
            if stack and stack[-1]["waiting"]:
                stack[-1]["arguments"].append(token.start())
                stack[-1]["waiting"] = False
            stack.append({"open": token.start(), "call": is_call, "arguments": [], "waiting": True, "kind": "("})
        elif value in ("[", "{"):
            if stack and stack[-1]["waiting"]:
                stack[-1]["arguments"].append(token.start())
                stack[-1]["waiting"] = False
            stack.append({"open": token.start(), "call": False, "arguments": [], "waiting": True, "kind": value})
        elif value in (")", "]", "}"):
            if stack:
                frame = stack.pop()
                if frame["call"] and "\n" in code[frame["open"]:token.start()]:
                    seen = set()
                    for offset in frame["arguments"]:
                        line = code.count("\n", 0, offset)
                        if line in seen:
                            yield offset
                        seen.add(line)
        elif stack:
            if value == ",":
                stack[-1]["waiting"] = True
            elif stack[-1]["waiting"]:
                stack[-1]["arguments"].append(token.start())
                stack[-1]["waiting"] = False


def function_definitions(source):
    """Locate this repository's C definitions without confusing comments or macros with code.

    This is deliberately lexical: balanced top-level bodies must follow a function
    declarator. Preprocessor logical lines are masked, not expanded. The report
    exposes the definition inventory for comparison with compiler/tool output.
    """
    code = NON_CODE.sub(blank_non_code, source)
    masked_lines = []
    continuation = False
    for line in code.splitlines(keepends=True):
        if continuation or line.lstrip().startswith("#"):
            continuation = line.rstrip("\r\n").endswith("\\")
            line = re.sub(r"[^\r\n]", " ", line)
        masked_lines.append(line)
    code = "".join(masked_lines)
    depth = 0
    boundary = 0
    definition = None
    for offset, character in enumerate(code):
        if character == "{":
            if depth == 0:
                prefix = code[boundary:offset].rstrip()
                if prefix.endswith(")"):
                    cursor = len(prefix) - 1
                    parentheses = 1
                    while cursor > 0 and parentheses:
                        cursor -= 1
                        if prefix[cursor] == ")":
                            parentheses += 1
                        elif prefix[cursor] == "(":
                            parentheses -= 1
                    name = re.search(r"\b([A-Za-z_]\w*)\s*$", prefix[:cursor])
                    if name and not parentheses:
                        start = boundary + len(prefix) - len(prefix.lstrip())
                        definition = {"name": name[1], "start": start, "body": offset}
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                if definition is not None:
                    definition["end"] = offset + 1
                    yield definition
                    definition = None
                boundary = offset + 1
        elif character == ";" and depth == 0:
            boundary = offset + 1


def definition_documentation(source, definition):
    """Return the directly attached Doxygen block and its required separator."""
    prefix = source[:definition["start"]].rstrip()
    end = len(prefix)
    start = prefix.rfind("/*")
    comment = prefix[start:] if start >= 0 and prefix.endswith("*/") else ""
    if not comment.startswith("/**"):
        return {"start": end, "end": end, "comment": "", "has_separator": False}
    before = prefix[:start].rstrip()
    return {"start": start, "end": end, "comment": comment,
            "has_separator": before.endswith(FUNCTION_SEPARATOR)}


def scan(path):
    raw = path.read_bytes()
    source = raw.decode("utf-8-sig")
    code = NON_CODE.sub(blank_non_code, source)
    lines = source.split("\n")
    findings = []
    boundaries = []
    definitions = list(function_definitions(source))

    def location(rule, offset, kind):
        line = source.count("\n", 0, offset) + 1
        kind.append({"rule": rule, "line": line, "source": lines[line - 1].strip()})

    for rule, pattern in RULES.items():
        for match in re.finditer(pattern, code):
            location(rule, match.start(), findings)
    for rule, pattern in BOUNDARIES.items():
        for match in re.finditer(pattern, code):
            location(rule, match.start(), boundaries)
    for match in re.finditer(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]", source):
        location("source-control-character", match.start(), findings)
    for token in NON_CODE.finditer(source):
        if token[0].startswith(("/*", "//")):
            for tag in re.finditer(r"(?:\\|@)returns?\b", token[0]):
                location("retval-documentation", token.start() + tag.start(), findings)
    for offset in packed_argument_offsets(source):
        location("multiline-argument-packing", offset, findings)
    if path.stem.startswith("utl_"):
        for match in re.finditer(r"^\s*#include\s*[<\"](?:windows|windowsx|intrin|winsock2|winhttp)\.h[>\"]", source, re.M | re.I):
            location("utility-platform-dependency", match.start(), findings)
        for match in re.finditer(r"\b(?:LPCTSTR|LPTSTR|TCHAR|DWORD|HANDLE|HRESULT|GetCommandLine[AW]?|SetErrorMode|ExitProcess|__fastfail|__declspec)\b", code):
            location("utility-platform-dependency", match.start(), findings)
    if path.stem == "utl_arguments":
        for match in re.finditer(r"\bUtl_Arguments_Read(?:Windows|Win32|Posix)\b", code):
            location("argument-public-api-name", match.start(), findings)
        if path.suffix == ".c":
            for match in re.finditer(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b", code, re.M):
                location("argument-build-selection", match.start(), findings)
    for match in re.finditer(r"(?<!\r)\n", source):
        location("crlf", match.start(), findings)
        break
    for match in re.finditer("\t", code):
        location("tab", match.start(), findings)
    expected = HEADER_SECTIONS if path.suffix == ".h" else SOURCE_SECTIONS
    last = -1
    for section in expected:
        offset = source.find("** " + section)
        if offset < 0:
            findings.append({"rule": "source-sections", "line": 1, "source": "Missing " + section})
        elif offset < last:
            location("source-section-order", offset, findings)
        else:
            last = offset
    macros_start = source.find("** 3.1 Macros")
    macros_end = source.find("** 3.2 Types")
    for match in re.finditer(r"^#define\s+(\w+)([^\r\n]*)", code, re.M):
        is_guard = path.suffix == ".h" and match.start() < source.find("** 1. References") and not match[2].strip()
        if not is_guard and not (0 <= macros_start < match.start() < macros_end):
            location("macro-section", match.start(), findings)
    for match in re.finditer(r"\b0\s*!=\s*\([^;\r\n]*?(?<!&)&(?!&)[^;\r\n]*?\)", code):
        location("bitmask-zero-comparison", match.start(), findings)
    for definition in definitions:
        body_start = definition["body"] + 1
        body_end = definition["end"] - 1
        body_code = code[body_start:body_end]
        for statement in re.finditer(r"\breturn\b[^;]*;", body_code):
            prefix = body_code[:statement.start()]
            at_function_scope = prefix.count("{") == prefix.count("}")
            is_final_statement = not body_code[statement.end():].strip()
            if not (at_function_scope and is_final_statement):
                location("nonfinal-return", body_start + statement.start(), findings)
        if path.stem.startswith("utl_"):
            body = source[definition["body"]:definition["end"]]
            for token in COMMENTS_AND_STRINGS.finditer(body):
                if token.group("character") is not None:
                    location("named-grammar-character", definition["body"] + token.start(), findings)
        documentation = definition_documentation(source, definition)
        comment = documentation["comment"]
        details = re.search(r"^ \* \\par Details:\s*\r?\n \*  \S", comment, re.M)
        if not details:
            location("function-definition-details", definition["start"], findings)
        if not documentation["has_separator"]:
            location("function-definition-separator", definition["start"], findings)
    if path.suffix == ".h":
        public_functions = [
            (m[1], m.start()) for m in PUBLIC_DECLARATION.finditer(code)
        ]
    else:
        public_functions = [
            (d["name"], d["start"]) for d in definitions
            if re.fullmatch(r"(?:[A-Z][A-Za-z0-9]*_)+[A-Za-z0-9]+", d["name"])
            and not code[d["start"]:d["body"]].lstrip().startswith("static ")
        ]
    saw_find = False
    saw_io = False
    saw_operation = set()
    for name, offset in public_functions:
        namespace = name.rsplit("_", 1)[0]
        operation = name.rsplit("_", 1)[-1]
        if operation.startswith("Initialize"):
            if namespace in saw_operation:
                location("public-initialize-first", offset, findings)
        else:
            saw_operation.add(namespace)
        if not PUBLIC_OPERATION.match(operation):
            location("public-operation-verb", offset, findings)
        if operation.startswith(("Read", "Write")):
            saw_io = True
        if operation.startswith("Find"):
            saw_find = True
        query = re.search(r"^(?:Is|Has|Can|Get|Needs|Count|Calculate|Compute|Measure|Derive|Resolve|Inspect)|(?:ByteCount|Size|Length|Capacity|Equals)$", operation)
        if query and saw_find:
            location("query-derive-before-find", offset, findings)
        if query and saw_io:
            location("query-derive-before-read-write", offset, findings)
        if operation.startswith("Find") and saw_io:
            location("find-before-read-write", offset, findings)
    header_path = path.with_suffix(".h")
    if path.parent.name in ("win32", "posix"):
        header_path = path.parent.parent / header_path.name
    if path.suffix == ".c" and public_functions and header_path.exists():
        header = header_path.read_text(encoding="utf-8-sig")
        header_code = NON_CODE.sub(blank_non_code, header)
        implemented = [name for name, _ in public_functions]
        namespaces = {name.rsplit("_", 1)[0] for name in implemented}
        declared = [
            m[1] for m in PUBLIC_DECLARATION.finditer(header_code)
            if m[1].rsplit("_", 1)[0] in namespaces
        ]
        if declared != implemented:
            location("public-api-definition-order", public_functions[0][1], findings)
    return {
        "path": path.relative_to(ROOT).as_posix(),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "lines": len(lines),
        "function_definitions": [
            {"name": d["name"], "line": source.count("\n", 0, d["start"]) + 1} for d in definitions
        ],
        "findings": findings,
        "boundaries_requiring_lifecycle_review": boundaries,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=pathlib.Path, required=True)
    parser.add_argument("--strict", action="store_true", help="Fail if any scanned convention has a finding.")
    parser.add_argument("--strict-api", action="store_true",
                        help="Fail on public API naming/ordering findings; retain the complete audit report.")
    args = parser.parse_args()
    files = sorted(p for p in SOURCE.rglob("*") if p.suffix in (".c", ".h"))
    results = [scan(p) for p in files]
    counts = collections.Counter(f["rule"] for r in results for f in r["findings"])
    boundary_counts = collections.Counter(
        f["rule"] for r in results for f in r["boundaries_requiring_lifecycle_review"]
    )
    report = {
        "scope": "All CastDemo C sources and headers; no per-file or per-rule exemption.",
        "limits": [
            "Lexical findings require semantic review; the detector is not a complete C parser.",
            "An allocation or I/O call is a boundary to review, not automatic proof of its runtime phase.",
            "Missing findings do not establish MISRA compliance, realtime behavior or a CRT-free executable.",
            "Contracts, specification coverage, ownership, state machines, indirect dependency calls,",
            "alignment, declarations, Win32 dispatch, accessibility, and runtime behavior need additional review.",
        ],
        "file_count": len(results),
        "line_count": sum(r["lines"] for r in results),
        "function_definition_count": sum(len(r["function_definitions"]) for r in results),
        "findings_by_rule": dict(sorted(counts.items())),
        "boundaries_by_kind": dict(sorted(boundary_counts.items())),
        "files": results,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("w", encoding="utf-8", newline="\r\n") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps({k: report[k] for k in ("file_count", "line_count", "function_definition_count", "findings_by_rule", "boundaries_by_kind")}, indent=2))
    api_failure = any(
        rule.startswith(("public-", "query-derive-", "find-before-"))
        or rule in ("os-public-prefix", "crt-public-prefix")
        for rule in counts
    )
    return int((args.strict and bool(counts)) or (args.strict_api and api_failure))


if __name__ == "__main__":
    raise SystemExit(main())
