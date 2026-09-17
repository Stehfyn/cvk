/* Integer parsing checks; no Vulkan loader is initialized. */
#include "cvk_json.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                                                                                                                       \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (!(x))                                                                                                                                                                                      \
        {                                                                                                                                                                                              \
            (void)fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                                                                                                      \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
    } while (0)

static int64_t parse_signed(const char* text)
{
    cvkJsonValue value;

    memset(&value, 0, sizeof(value));
    value.kind       = CVK_JSON_STRING;
    value.pText      = text;
    value.textLength = (uint32_t)strlen(text);
    return cvkGetJsonInt64(&value);
}

int main(void)
{
    CHECK(cvkParseUint64Text("18446744073709551615") == UINT64_MAX);
    CHECK(cvkParseUint64Text("18446744073709551616") == UINT64_MAX);
    CHECK(cvkParseUint64Text("0xffffffffffffffff") == UINT64_MAX);
    CHECK(cvkParseUint64Text("-1") == UINT64_MAX);
    CHECK(cvkParseUint64Text("  +077") == 63);
    CHECK(cvkParseUint64Text("4294967296") == ((uint64_t)1 << 32));
    CHECK(cvkParseUint64Text("0x100000001") == (((uint64_t)1 << 32) + 1));
    CHECK(cvkParseUint64Text("0x") == 0);
    CHECK(cvkParseUint64Text("bad") == 0);
    CHECK(cvkParseUint64Text("08") == 0);
    CHECK(parse_signed("9223372036854775807") == INT64_MAX);
    CHECK(parse_signed("9223372036854775808") == INT64_MAX);
    CHECK(parse_signed("-9223372036854775808") == INT64_MIN);
    CHECK(parse_signed("-9223372036854775809") == INT64_MIN);
    CHECK(parse_signed("-0x8000000000000000") == INT64_MIN);
    CHECK(parse_signed(" -077") == -63);
    CHECK(parse_signed("--1") == 0);
    CHECK(parse_signed("- 1") == 0);
    return 0;
}
