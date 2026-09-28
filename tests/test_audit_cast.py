"""Boundary cases for the CastDemo lexical documentation and ordering guard."""

import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("audit_cast", ROOT / "scripts/audit_cast.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class CastAuditTests(unittest.TestCase):
    def test_invisible_source_controls_include_comments_and_literals(self):
        source = "/* " + chr(8) + "rief damaged documentation */\n"
        source += r'/* \brief Escaped documentation is ordinary source text. */' + "\n"
        source += 'const char* damaged = "' + chr(12) + '";\n'
        source += r'const char* escaped = "\b\f";' + "\n"
        result = self.scan_text('.c', source)
        findings = [f for f in result['findings'] if f['rule'] == 'source-control-character']
        self.assertEqual([f['line'] for f in findings], [1, 3])
        self.assertIn('damaged', findings[1]['source'])

    def test_startup_allocation_wrappers_remain_lifetime_boundaries(self):
        source = ('void Startup(void) { OS_Memory_Initialize(&memory, 4096); CastSession_Initialize(&session); '
                  'OS_Network_Initialize(&network); Ssdp_Initialize(&discovery); '
                  'Proxy_Initialize(&proxy); MediaServer_Initialize(&serving); }\n')
        source += 'void Reuse(void) { CastSession_Prepare(&session, &config); }\n'
        result = self.scan_text('.c', source)
        boundaries = result['boundaries_requiring_lifecycle_review']
        allocations = [b for b in boundaries if b['rule'] == 'allocation-and-release']
        self.assertEqual(len(allocations), 6)
        self.assertTrue(all(b['line'] == 1 for b in allocations))

    def test_native_network_boundaries_are_not_hidden_by_retained_storage(self):
        source = 'void Open(void) { socket(AF_INET, SOCK_DGRAM, 0); bind(s, &address, length); }\n'
        source += 'void Read(void) { WSAIoctl(s, operation); recvfrom(s, buffer); sendto(s, buffer); accept(s, 0, 0); }\n'
        source += 'void Close(void) { closesocket(s); WinHttpCloseHandle(request); }\n'
        source += 'void Text(void) { Log("socket() WSAIoctl()"); /* bind(); */ }\n'
        result = self.scan_text('.c', source)
        boundaries = result['boundaries_requiring_lifecycle_review']
        resources = [b['line'] for b in boundaries if b['rule'] == 'thread-and-resource-lifetime']
        operations = [b['line'] for b in boundaries if b['rule'] == 'waiting-and-io']
        self.assertEqual(resources, [1, 1, 2, 3, 3])
        self.assertEqual(operations, [2, 2, 2, 2])

    def test_crt_character_set_scanners_are_reported(self):
        source = 'void Scan(void) { strcspn(input, stops); strspn(input, allowed); strpbrk(input, choices); }\n'
        source += 'void Text(void) { Log("strcspn(input, stops)"); /* strpbrk(input, choices); */ }\n'
        result = self.scan_text('.c', source)
        calls = [f for f in result['findings'] if f['rule'] == 'crt-call']
        self.assertEqual([f['line'] for f in calls], [1, 1, 1])

    def test_prepare_is_an_operation_verb(self):
        result = self.scan_text('.h', 'void CastSession_Prepare(void);\n')
        self.assertFalse(any(f['rule'] == 'public-operation-verb' for f in result['findings']))

    def test_crt_warning_disables_are_reported(self):
        source = '#define _CRT_SECURE_NO_WARNINGS\n#define _CRT_NONSTDC_NO_DEPRECATE\n'
        source += '/* #define _CRT_SECURE_NO_WARNINGS */\n'
        result = self.scan_text('.c', source)
        findings = [f for f in result['findings'] if f['rule'] == 'crt-warning-suppression']
        self.assertEqual([f['line'] for f in findings], [1, 2])

    def test_nonfinal_returns_include_void_guards_and_switch_exits(self):
        source = "int Read(int valid) { if (!valid) { return 0; } return 1; }\n"
        source += "void Write(int valid) { if (!valid) return; Commit(); }\n"
        source += "int Get(int kind) { switch (kind) { case 1: return 2; default: return 0; } }\n"
        result = self.scan_text(".c", source)
        findings = [f for f in result["findings"] if f["rule"] == "nonfinal-return"]
        self.assertEqual([f["line"] for f in findings], [1, 2, 3, 3])

    def test_final_results_and_return_text_in_literals_are_not_early_exits(self):
        source = 'int Read(int valid) { int result = 0; if (valid) { result = 1; } return result; }\n'
        source += 'void Write(void) { Log("return;"); /* return; */ }\n'
        source += 'int ReadEmpty(void) { return 0; /* Completed result. */ }\n'
        result = self.scan_text(".c", source)
        self.assertFalse(any(f["rule"] == "nonfinal-return" for f in result["findings"]))

    def test_inline_barriers_are_reported_without_matching_documentation(self):
        source = 'NO_INLINE int First(void); __declspec(noinline) int Second(void);\n'
        source += 'static FORCE_INLINE int Third(void); /* NO_INLINE is prohibited. */\n'
        result = self.scan_text(".c", source)
        self.assertEqual(sum(f["rule"] == "no-inline-barrier" for f in result["findings"]), 2)

    def test_initializers_precede_every_other_public_operation(self):
        for prefix in ("OS_RingBuffer", "OS_Atomic", "Utl_Http", "CastMessage"):
            wrong = f"uint32_t {prefix}_GetCount(void);\nvoid {prefix}_Initialize(void);\n"
            result = self.scan_text(".h", wrong)
            self.assertTrue(any(f["rule"] == "public-initialize-first" for f in result["findings"]))
            correct = f"void {prefix}_Initialize(void);\nuint32_t {prefix}_GetCount(void);\n"
            result = self.scan_text(".h", correct)
            self.assertFalse(any(f["rule"] == "public-initialize-first" for f in result["findings"]))

    def test_accessor_names_need_an_operation_prefix(self):
        for name in ("OS_RingBuffer_FreeByteCount", "OS_Queue_Capacity", "GpuDecode_Frame",
                     "Utl_Protobuf_FieldByteCount", "Mp4Demux_AudioSample"):
            result = self.scan_text(".h", f"const byte_t* {name}(void);\n")
            self.assertTrue(any(f["rule"] == "public-operation-verb" for f in result["findings"]))
            module, operation = name.rsplit("_", 1)
            result = self.scan_text(".h", f"const byte_t* {module}_Get{operation}(void);\n")
            self.assertFalse(any(f["rule"] == "public-operation-verb" for f in result["findings"]))

    def test_initialize_order_is_checked_in_definitions_too(self):
        text = "uint32_t OS_Queue_GetCapacity(void) { return 1; }\n"
        text += "void OS_Queue_Initialize(void) {}\n"
        result = self.scan_text(".c", text)
        self.assertTrue(any(f["rule"] == "public-initialize-first" for f in result["findings"]))

    def test_header_recognizes_qualified_pointer_and_multiple_initializer_signatures(self):
        text = "void OS_RingBuffer_Initialize(void);\nvoid OS_RingBuffer_InitializeEmpty(void);\n"
        text += "const byte_t* OS_RingBuffer_GetReadableSpan(uint32_t* count);\n"
        result = self.scan_text(".h", text)
        self.assertFalse(any(f["rule"] in ("public-initialize-first", "public-operation-verb")
                             for f in result["findings"]))

    def test_crt_namespace_uses_the_requested_uppercase_prefix(self):
        text = 'void Crt_Bad(void);\nvoid CRT_Good(void);\n'
        text += 'void crt_bad(void);\nvoid cRT_bad(void);\n'
        text += '/* Crt_ is quoted here. */\nconst char* label = "crt_quoted";\n'
        result = self.scan_text(".h", text, "crt_fixture")
        findings = [f for f in result["findings"] if f["rule"] == "crt-public-prefix"]
        self.assertEqual([f["line"] for f in findings], [1, 3, 4])

    def test_os_namespace_uses_the_explicit_uppercase_exception(self):
        text = 'void Os_Bad(void);\nvoid OS_Good(void);\n'
        text += '/* Os_ is only quoted here. */\nconst char* label = "Os_quoted";\n'
        result = self.scan_text(".h", text, "os_fixture")
        findings = [f for f in result["findings"] if f["rule"] == "os-public-prefix"]
        self.assertEqual([f["line"] for f in findings], [1])

    def test_argument_backends_keep_the_common_api_without_target_macros(self):
        text = '#if defined(_WIN32)\nbool_t Utl_Arguments_ReadWindows(void);\n#endif\n'
        result = self.scan_text(".c", text, "utl_arguments")
        rules = [f["rule"] for f in result["findings"]]
        self.assertIn("argument-public-api-name", rules)
        self.assertIn("argument-build-selection", rules)
        result = self.scan_text(".c", 'bool_t Utl_Arguments_Read(void) { return TRUE; }\n', "utl_arguments")
        self.assertFalse(any(f["rule"].startswith("argument-") for f in result["findings"]))

    def test_backend_definition_order_is_compared_with_the_shared_header(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = pathlib.Path(directory)
            (path / "win32").mkdir()
            (path / "utl_fixture.h").write_bytes(b'bool_t Utl_Fixture_Read(void);\r\n')
            source = path / "win32/utl_fixture.c"
            source.write_bytes(b'bool_t Utl_Fixture_Write(void) { return TRUE; }\r\n')
            result = AUDIT.scan(source)
            self.assertTrue(any(f["rule"] == "public-api-definition-order" for f in result["findings"]))

    def test_utility_platform_dependencies_are_reported(self):
        text = '#include <windows.h>\n'
        text += 'void Utl_Bad(void) { LPCTSTR value = GetCommandLine(); ExitProcess(0); }\n'
        result = self.scan_text(".c", text, "utl_fixture")
        self.assertEqual(sum(f["rule"] == "utility-platform-dependency" for f in result["findings"]), 4)
        result = self.scan_text(".c", text, "os_backend_fixture")
        self.assertFalse(any(f["rule"] == "utility-platform-dependency" for f in result["findings"]))

    def test_resource_inventory_includes_generic_and_explicit_win32_names(self):
        text = 'void Initialize(void) { CreateEvent(); CreateEventA(); CreateEventW(); }\n'
        result = self.scan_text(".c", text)
        findings = [f for f in result["boundaries_requiring_lifecycle_review"] if f["rule"] == "thread-and-resource-lifetime"]
        self.assertEqual(len(findings), 3)

    def test_result_tags_use_retval_and_do_not_scan_string_literals(self):
        text = r'''/** \return TRUE for success. */
/** \returns A count. */
/// @return Incorrect spelling.
/** @returns Incorrect spelling. */
/** \retval TRUE Success.
 *  \retval FALSE Invalid data.
 */
const char* example = "\\return is test data";
'''
        result = self.scan_text(".h", text)
        findings = [f for f in result["findings"] if f["rule"] == "retval-documentation"]
        self.assertEqual([f["line"] for f in findings], [1, 2, 3, 4])

    def test_wrapped_arguments_each_start_on_their_own_line(self):
        packed = 'void Bad(int first, int second,\n         int third);\n'
        packed += 'Bad("first, literal", second,\n    Nested(third, fourth));\n'
        result = self.scan_text(".c", packed)
        findings = [f for f in result["findings"] if f["rule"] == "multiline-argument-packing"]
        self.assertEqual([f["line"] for f in findings], [1, 3])
        correct = 'void Good(int first,\n          int second,\n          int third);\n'
        correct += 'Good("first, literal",\n     Nested(second, third),\n     (Value){fourth, fifth});\n'
        correct += 'OneLine(first, second, third);\nif (first &&\n    second) {}\n'
        correct += 'Good(first, /* comment with commas , , */\n     second,\n     third);\n'
        result = self.scan_text(".c", correct)
        self.assertFalse(any(f["rule"] == "multiline-argument-packing" for f in result["findings"]))

    def test_definitions_exclude_macro_bodies_prototypes_and_initializers(self):
        source = r'''
#define BODY(x) \
    int NotAFunction(void) { return (x); }
static int Forward(int (*callback)(int));
struct Item { int value; };
static struct Item items[] = {{1}, {2}};
/* void InComment(void) {} */
static int First(int (*callback)(int))
{
    const char* text = "void InString(void) {}";
    if (text) { return callback(1); }
    return 0;
}
int Second(void) { return 2; }
'''
        self.assertEqual([d["name"] for d in AUDIT.function_definitions(source)], ["First", "Second"])

    def test_definition_comment_must_be_directly_attached(self):
        source = AUDIT.FUNCTION_SEPARATOR + "\n/**\n * \\par Details:\n *  None\n */\n"
        source += "int value;\nvoid Missing(void) {}\n"
        definition = next(AUDIT.function_definitions(source))
        self.assertFalse(AUDIT.definition_documentation(source, definition)["comment"])

    def scan_text(self, suffix, text, stem="fixture"):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = pathlib.Path(directory) / (stem + suffix)
            path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))
            return AUDIT.scan(path)

    def test_multiline_details_and_separator_are_both_required(self):
        text = AUDIT.FUNCTION_SEPARATOR + "\n/**\n * \\par Details:\n *  None\n */\nvoid Valid(void) {}\n"
        text += "/** \\par Details: None */\nvoid Inline(void) {}\n"
        result = self.scan_text(".c", text)
        findings = [f for f in result["findings"] if f["rule"].startswith("function-definition-")]
        self.assertEqual(len(findings), 2)
        self.assertTrue(all("Inline" in f["source"] for f in findings))

    def test_query_after_varint_io_is_rejected_even_before_field_io(self):
        text = "bool_t Utl_Wire_ReadVarint(void);\nuint32_t Utl_Wire_FieldByteCount(void);\nbool_t Utl_Wire_ReadField(void);\n"
        result = self.scan_text(".h", text)
        findings = [f for f in result["findings"] if f["rule"] == "query-derive-before-read-write"]
        self.assertEqual(len(findings), 1)
        self.assertIn("FieldByteCount", findings[0]["source"])

    def test_query_before_all_io_is_accepted(self):
        text = "uint32_t Utl_Wire_FieldByteCount(void);\nbool_t Utl_Wire_ReadVarint(void);\nuint32_t Utl_Wire_WriteField(void);\n"
        result = self.scan_text(".h", text)
        self.assertFalse(any(f["rule"] == "query-derive-before-read-write" for f in result["findings"]))

    def test_find_follows_queries_and_precedes_io(self):
        wrong = "uint32_t Utl_Http_FindField(void);\nuint32_t Utl_Http_HeadByteCount(void);\nbool_t Utl_Http_ResolveRange(void);\n"
        result = self.scan_text(".h", wrong)
        self.assertEqual(sum(f["rule"] == "query-derive-before-find" for f in result["findings"]), 2)
        correct = "uint32_t Utl_Http_HeadByteCount(void);\nbool_t Utl_Http_ResolveRange(void);\nuint32_t Utl_Http_FindField(void);\nbool_t Utl_Http_ReadHead(void);\n"
        result = self.scan_text(".h", correct)
        self.assertFalse(any(f["rule"] in ("query-derive-before-find", "find-before-read-write") for f in result["findings"]))
        result = self.scan_text(".h", "bool_t Utl_Http_ReadHead(void);\nuint32_t Utl_Http_FindField(void);\n")
        self.assertTrue(any(f["rule"] == "find-before-read-write" for f in result["findings"]))

    def test_loop_rules_include_macros_and_exclude_comments_and_strings(self):
        text = '/* while (x) {} do {} while (x); */\n'
        text += 'const char* example = "while do";\n'
        text += '#define WRAP(x) do { x; } while (0)\n'
        text += 'void Bad(void) { while (Ready()) { Step(); } }\n'
        text += 'void Good(void) { for (;;) { if (Done()) break; Step(); } }\n'
        result = self.scan_text(".c", text)
        findings = [f for f in result["findings"] if f["rule"] in ("while-loop", "do-while-loop")]
        self.assertEqual([f["line"] for f in findings if f["rule"] == "do-while-loop"], [3])
        self.assertEqual([f["line"] for f in findings if f["rule"] == "while-loop"], [3, 4])

    def test_console_input_and_retained_handles_are_lifetime_boundaries(self):
        text = "void Exercise(void) {\n"
        text += "OS_Console_Initialize(console);\nDuplicateHandle(process, input);\n"
        text += "OS_Console_ReadKey(console, key);\nReadConsoleInputEx(input, record);\n"
        text += "SetConsoleMode(input, mode);\nOS_Console_TryClose(console);\n}\n"
        result = self.scan_text(".c", text)
        boundaries = result["boundaries_requiring_lifecycle_review"]
        resources = [b["line"] for b in boundaries if b["rule"] == "thread-and-resource-lifetime"]
        operations = [b["line"] for b in boundaries if b["rule"] == "waiting-and-io"]
        self.assertEqual(resources, [2, 3, 7])
        self.assertEqual(operations, [4, 5, 6])

    def test_cast_sources_and_tests_have_no_prohibited_loops(self):
        paths = sorted(AUDIT.SOURCE.rglob("*.c")) + sorted(AUDIT.SOURCE.rglob("*.h"))
        paths += sorted((ROOT / "tests").glob("cast*.c"))
        for path in paths:
            with self.subTest(path=path.relative_to(ROOT)):
                source = path.read_text(encoding="utf-8-sig")
                code = AUDIT.NON_CODE.sub(AUDIT.blank_non_code, source)
                self.assertNotRegex(code, r"\b(?:while|do)\b")
                for token in AUDIT.NON_CODE.finditer(source):
                    if token[0].startswith(("/*", "//")):
                        self.assertNotRegex(token[0], r"(?:\\|@)returns?\b")

    def test_named_grammar_characters_distinguish_bodies_from_tables_and_macros(self):
        text = r'''#define UTL_WIRE_CHAR_EQUALS ('=')
static const char table[] = {'A', 'Z'};
char Utl_Wire_Read(void)
{
    /* The literal '=' is described here. */
    const char* quoted = "apostrophe ' and escaped quote \"";
    char byte = '\'';
    char line = '\n';
    char wide = L'x';
    return '=';
}
'''
        result = self.scan_text(".c", text, "utl_fixture")
        findings = [f for f in result["findings"] if f["rule"] == "named-grammar-character"]
        self.assertEqual([f["line"] for f in findings], [7, 8, 9, 10])
        result = self.scan_text(".c", text, "application_fixture")
        self.assertFalse(any(f["rule"] == "named-grammar-character" for f in result["findings"]))


if __name__ == "__main__":
    unittest.main()
