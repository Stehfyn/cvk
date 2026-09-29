"""Native protobuf/CastV2 regression vectors, compiled under the owned C policy."""

import ctypes as ct
from pathlib import Path
from random import Random
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BYTE = ct.c_uint8
U32 = ct.c_uint32
U64 = ct.c_uint64
BYTE_POINTER = ct.POINTER(BYTE)


class Field(ct.Structure):
    _fields_ = [("data", BYTE_POINTER), ("value", U64), ("number", U32),
                ("wire", ct.c_int), ("count", U32), ("encoded_count", U32)]


class Span(ct.Structure):
    _fields_ = [("data", BYTE_POINTER), ("count", U32)]


class Message(ct.Structure):
    _fields_ = [("source", Span), ("destination", Span), ("namespace", Span),
                ("payload_type", ct.c_int), ("payload", Span)]


EXPORTS = {
    "Utl_Protobuf_ReadVarint": (ct.c_int, [ct.POINTER(U64), ct.POINTER(U32), BYTE_POINTER, U32]),
    "Utl_Protobuf_WriteVarint": (U32, [U64, BYTE_POINTER, U32]),
    "Utl_Protobuf_GetFieldByteCount": (U32, [ct.POINTER(Field)]),
    "Utl_Protobuf_ReadField": (ct.c_int, [ct.POINTER(Field), BYTE_POINTER, U32]),
    "Utl_Protobuf_WriteField": (U32, [ct.POINTER(Field), BYTE_POINTER, U32]),
    "Utl_Protobuf_SkipField": (ct.c_int, [ct.POINTER(U32), BYTE_POINTER, U32]),
    "Utl_CastV2_ReadMessage": (ct.c_int, [ct.POINTER(Message), BYTE_POINTER, U32]),
    "Utl_CastV2_WriteMessage": (U32, [ct.POINTER(Message), BYTE_POINTER, U32]),
}


class WireReaderTests(unittest.TestCase):
    def test_native_vectors(self):
        clang = shutil.which("clang")
        tidy = shutil.which("clang-tidy")
        self.assertIsNotNone(clang, "Native C fixtures require Clang")
        self.assertIsNotNone(tidy, "Native C fixtures require clang-tidy")
        source_root = ROOT / "examples/CastDemo"
        sources = [source_root / "utl" / f"utl_{name}.c"
                   for name in ("protobuf", "unicode", "castv2")]
        sources.append(source_root / "os/clang/os_assert.c")
        arguments = ["-std=c11", "-O3", "-Weverything", "-Werror", "-Wno-padded",
                     "-Wno-documentation", "-Wno-unsafe-buffer-usage", "-I" + str(source_root)]
        for source in sources:
            subprocess.run([tidy, str(source), "--config-file=" + str(source_root / ".clang-tidy"),
                            "--", *arguments], check=True, capture_output=True, text=True)
        with tempfile.TemporaryDirectory(prefix="cvk-wire-") as directory:
            library = Path(directory) / ("wire.dll" if sys.platform == "win32" else "wire.so")
            linking = (["-fuse-ld=lld", "-Wl,/NOENTRY"] +
                       ["-Wl,/EXPORT:" + name for name in EXPORTS]
                       if sys.platform == "win32" else ["-fPIC"])
            subprocess.run([clang, *arguments, "-shared", "-nostdlib", *linking,
                            *map(str, sources), "-o", str(library)],
                           check=True, capture_output=True, text=True)
            # A separate process releases the Windows DLL before temporary-file cleanup.
            subprocess.run([sys.executable, str(Path(__file__).resolve()), str(library)], check=True)


def wire_bytes(data):
    return (BYTE * len(data)).from_buffer_copy(data)


def check_library(path):
    library = ct.CDLL(path)
    for name, (result, arguments) in EXPORTS.items():
        function = getattr(library, name)
        function.restype = result
        function.argtypes = arguments
    read_varint = library.Utl_Protobuf_ReadVarint
    read_field = library.Utl_Protobuf_ReadField
    write_field = library.Utl_Protobuf_WriteField
    skip_field = library.Utl_Protobuf_SkipField
    output = (BYTE * 128)()
    random = Random(9247)
    values = [0, 1, 127, 128, 150, 16383, 16384, 2**32 - 1, 2**63, 2**64 - 1]
    values += [random.randrange(2**64) for _ in range(4096)]
    for value in values:
        # Independent base-128 digit representation; no native writer supplies the oracle.
        digits = max(1, (value.bit_length() + 6) // 7)
        expected = bytes(((value // 128**index) % 128) | (128 if index + 1 < digits else 0)
                         for index in range(digits))
        encoded = wire_bytes(expected)
        decoded, count = U64(43), U32(47)
        assert read_varint(ct.byref(decoded), ct.byref(count), encoded, len(encoded))
        assert (decoded.value, count.value) == (value, digits)
        for length in range(digits):
            assert not read_varint(ct.byref(decoded), ct.byref(count), encoded, length)
            assert (decoded.value, count.value) == (value, digits)
        written = library.Utl_Protobuf_WriteVarint(value, output, len(output))
        assert bytes(output[:written]) == expected
    for encoded in (b"\x80" * 10, b"\xff" * 9 + b"\x02", b"\xff" * 9 + b"\x81"):
        decoded, count = U64(43), U32(47)
        assert not read_varint(ct.byref(decoded), ct.byref(count), wire_bytes(encoded), len(encoded))
        assert (decoded.value, count.value) == (43, 47)

    # Independent tag/payload vectors cover every wire type and little-endian integers.
    for encoded, wire, value, payload in (
        (b"\x08\x96\x01", 0, 150, b""),
        (b"\x09\x08\x07\x06\x05\x04\x03\x02\x01", 1, 0x0102030405060708, b""),
        (b"\x0a\x03abc", 2, 0, b"abc"), (b"\x0b", 3, 0, b""),
        (b"\x0c", 4, 0, b""), (b"\x0d\x04\x03\x02\x01", 5, 0x01020304, b""),
    ):
        field = Field()
        source = wire_bytes(encoded)
        assert read_field(ct.byref(field), source, len(source))
        assert (field.number, field.wire, field.value, field.count) == (1, wire, value, len(payload))
        if payload:
            assert ct.string_at(field.data, field.count) == payload
        saved = bytes(field)
        for length in range(len(encoded)):
            assert not read_field(ct.byref(field), source, length)
            assert bytes(field) == saved
            ct.memset(output, 0xA5, len(output))
            assert not write_field(ct.byref(field), output, length)
            assert bytes(output) == b"\xa5" * len(output)
        written = write_field(ct.byref(field), output, len(output))
        assert bytes(output[:written]) == encoded
    for wire in (-1, 6, 7, 255):
        field = Field(number=1, wire=wire)
        assert library.Utl_Protobuf_GetFieldByteCount(ct.byref(field)) == 0
    for depth in (1, 2, 63, 64, 65):
        encoded = b"\x0b" * depth + b"\x08\x01" + b"\x0c" * depth
        count = U32(0xA5)
        accepted = skip_field(ct.byref(count), wire_bytes(encoded), len(encoded))
        assert bool(accepted) == (depth <= 64)
        assert count.value == (len(encoded) if accepted else 0xA5)
        if accepted:
            for length in range(len(encoded)):
                assert not skip_field(ct.byref(count), wire_bytes(encoded), length)
                assert count.value == len(encoded)
    for encoded in (b"\x0c", b"\x0b\x14", b"\x00", b"\x0e", b"\x0a\xff" * 5):
        count = U32(0xA5)
        assert not skip_field(ct.byref(count), wire_bytes(encoded), len(encoded))
        assert count.value == 0xA5

    # Required envelope fields, followed by a text payload containing two ASCII bytes.
    routing = b"\x08\x00\x12\x01s\x1a\x01d\x22\x01n\x28\x00"
    message_bytes = routing + b"\x32\x02{}"
    invalid = [routing, routing + b"\x3a\x00", message_bytes + b"\x3a\x00",
               routing + b"\x32\x01\xff" + b"\x32\x02{}", message_bytes + b"\x44"]
    for start, width in ((0, 2), (2, 3), (5, 3), (8, 3), (11, 2)):
        invalid.append(message_bytes[:start] + message_bytes[start + width:])
    for encoded in invalid:
        message = Message()
        saved = bytes(message)
        assert not library.Utl_CastV2_ReadMessage(ct.byref(message), wire_bytes(encoded), len(encoded))
        assert bytes(message) == saved
    unknown = b"\x40\x01\x4b\x50\x01\x4c"
    for encoded in (message_bytes, message_bytes + unknown,
                    message_bytes + b"\x12\x01t", message_bytes + b"\x28\x7f"):
        message = Message()
        source = wire_bytes(encoded)
        assert library.Utl_CastV2_ReadMessage(ct.byref(message), source, len(source))
        assert message.payload_type == 0
        assert ct.string_at(message.payload.data, message.payload.count) == b"{}"
        written = library.Utl_CastV2_WriteMessage(ct.byref(message), output, len(output))
        expected = message_bytes.replace(b"\x12\x01s", b"\x12\x01t") if encoded.endswith(b"\x12\x01t") else message_bytes
        assert bytes(output[:written]) == expected
    print(f"Passed {len(values)} varints, six wire forms, group bounds, and CastV2 schema/failure vectors")


if __name__ == "__main__":
    if len(sys.argv) == 2 and Path(sys.argv[1]).suffix in (".dll", ".so"):
        check_library(sys.argv[1])
    else:
        unittest.main()
