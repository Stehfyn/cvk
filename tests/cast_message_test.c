/* CASTV2 framing, HTTP range parsing and receiver-status JSON checks; no network, no Vulkan. */
#include "cast_message.h"
#include "cvk_json.h"
#include "media_server.h"

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

static int TestEncodeKnownBytes(void)
{
    /* 08 00 | 12 01 's' | 1A 01 'd' | 22 01 'n' | 28 00 | 32 02 '{' '}'  = 17 bytes */
    static const uint8_t expected[] = {0, 0, 0, 17, 0x08, 0x00, 0x12, 0x01, 's', 0x1A, 0x01, 'd', 0x22, 0x01, 'n', 0x28, 0x00, 0x32, 0x02, '{', '}'};
    CastMessage          message;
    uint8_t              frame[64];
    size_t               length;

    CastMessage_Set(&message, "s", "d", "n", "{}");
    length = CastMessage_Encode(&message, frame, sizeof(frame));
    CHECK(length == sizeof(expected));
    CHECK(0 == memcmp(frame, expected, sizeof(expected)));
    CHECK(0 == CastMessage_Encode(&message, frame, sizeof(expected) - 1));
    return 0;
}

static int TestRoundTripLongPayload(void)
{
    static uint8_t frame[CAST_MESSAGE_MAX_FRAME];
    char           payload[300];
    CastMessage    message;
    CastMessage    decoded;
    size_t         length;
    size_t         index;

    for (index = 0; index < sizeof(payload) - 1; ++index)
    {
        payload[index] = (char)('a' + (index % 26));
    }
    payload[sizeof(payload) - 1] = 0;
    CastMessage_Set(&message, "sender-0", "receiver-0", "urn:x-cast:com.google.cast.receiver", payload);
    length = CastMessage_Encode(&message, frame, sizeof(frame));
    CHECK(length > 4);
    CHECK((((size_t)frame[0] << 24) | ((size_t)frame[1] << 16) | ((size_t)frame[2] << 8) | frame[3]) == length - 4);
    CHECK(CastMessage_Decode(frame + 4, length - 4, &decoded));
    CHECK(decoded.sourceLength == 8 && 0 == memcmp(decoded.pSource, "sender-0", 8));
    CHECK(decoded.destinationLength == 10 && 0 == memcmp(decoded.pDestination, "receiver-0", 10));
    CHECK(CastMessage_NamespaceIs(&decoded, "urn:x-cast:com.google.cast.receiver"));
    CHECK(!CastMessage_NamespaceIs(&decoded, "urn:x-cast:com.google.cast.media"));
    CHECK(decoded.payloadLength == sizeof(payload) - 1 && 0 == memcmp(decoded.pPayload, payload, sizeof(payload) - 1));
    CHECK(0 == decoded.binary);
    /* truncated bodies are rejected */
    CHECK(!CastMessage_Decode(frame + 4, length - 5, &decoded));
    return 0;
}

static int TestDecodeSkipsUnknownFields(void)
{
    /* protocol_version, source, destination, unknown fixed64 (field 9), unknown fixed32 (field 10),
    ** namespace, payload_type BINARY, payload_binary */
    static const uint8_t body[] = {0x08, 0x00, 0x12, 0x01, 'a',  0x1A, 0x01, 'b',  0x49, 1,    2,    3,   4,    5,    6,    7, 8,
                                   0x55, 1,    2,    3,    4,    0x22, 0x01, 'n',  0x28, 0x01, 0x3A, 0x02, 0xFF, 0x00};
    CastMessage          decoded;

    CHECK(CastMessage_Decode(body, sizeof(body), &decoded));
    CHECK(decoded.binary);
    CHECK(decoded.payloadLength == 2 && 0xFF == (uint8_t)decoded.pPayload[0]);
    CHECK(CastMessage_NamespaceIs(&decoded, "n"));
    /* a different protocol version is not understood */
    {
        static const uint8_t wrongVersion[] = {0x08, 0x01, 0x12, 0x01, 'a', 0x1A, 0x01, 'b', 0x22, 0x01, 'n', 0x32, 0x00};

        CHECK(!CastMessage_Decode(wrongVersion, sizeof(wrongVersion), &decoded));
    }
    /* a body missing the namespace is incomplete */
    {
        static const uint8_t noNamespace[] = {0x08, 0x00, 0x12, 0x01, 'a', 0x1A, 0x01, 'b', 0x32, 0x00};

        CHECK(!CastMessage_Decode(noNamespace, sizeof(noNamespace), &decoded));
    }
    return 0;
}

static int TestRanges(void)
{
    uint64_t first = 7;
    uint64_t last  = 7;

    CHECK(1 == MediaServer_ParseRange("bytes=0-99", 1000, &first, &last) && 0 == first && 99 == last);
    CHECK(1 == MediaServer_ParseRange(" bytes=500-", 1000, &first, &last) && 500 == first && 999 == last);
    CHECK(1 == MediaServer_ParseRange("bytes=-100", 1000, &first, &last) && 900 == first && 999 == last);
    CHECK(1 == MediaServer_ParseRange("bytes=-5000", 1000, &first, &last) && 0 == first && 999 == last);
    CHECK(1 == MediaServer_ParseRange("bytes=0-5000", 1000, &first, &last) && 0 == first && 999 == last);
    CHECK(1 == MediaServer_ParseRange("bytes=10-20, 30-40", 1000, &first, &last) && 10 == first && 20 == last);
    CHECK(-1 == MediaServer_ParseRange("bytes=1000-", 1000, &first, &last));
    CHECK(-1 == MediaServer_ParseRange("bytes=-1", 0, &first, &last));
    CHECK(0 == MediaServer_ParseRange("items=0-1", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=5-2", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=-", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=a-b", 1000, &first, &last));
    CHECK(0 == strcmp("video/mp4", MediaServer_ContentTypeForExtension(L".MP4")));
    CHECK(0 == strcmp("video/x-matroska", MediaServer_ContentTypeForExtension(L".mkv")));
    CHECK(0 == strcmp("application/octet-stream", MediaServer_ContentTypeForExtension(L".bin")));
    CHECK(0 == strcmp("application/octet-stream", MediaServer_ContentTypeForExtension(NULL)));
    return 0;
}

/* The demo reads receiver replies with the cvk JSON reader; check the lookups it relies on. */
static int TestReceiverStatusJson(void)
{
    static const char   text[] = "{\"requestId\":1,\"status\":{\"applications\":[{\"appId\":\"E8C28D3C\",\"displayName\":\"Backdrop\",\"transportId\":\"x\"},"
                                 "{\"appId\":\"CC1AD845\",\"displayName\":\"Default Media Receiver\",\"sessionId\":\"a1b2\",\"statusText\":\"Ready To Cast\","
                                 "\"transportId\":\"web-5\",\"namespaces\":[{\"name\":\"urn:x-cast:com.google.cast.media\"}]}],"
                                 "\"volume\":{\"level\":0.75,\"muted\":false}},\"type\":\"RECEIVER_STATUS\"}";
    static cvkJsonValue values[256];
    cvkJsonDocument     document;
    const cvkJsonValue* pApplications;
    const cvkJsonValue* pApplication;
    const cvkJsonValue* pTransport;

    document.valueCount = cvkParseJsonDocument(text, sizeof(text) - 1, values, 256);
    document.pValues    = values;
    CHECK(document.valueCount > 0);
    CHECK(cvkJsonStringEquals(cvkGetJsonMember(&document, &values[0], "type"), "RECEIVER_STATUS"));
    pApplications = cvkGetJsonMember(&document, cvkGetJsonMember(&document, &values[0], "status"), "applications");
    CHECK(NULL != pApplications && 2 == pApplications->childCount);
    pApplication = cvkGetJsonElement(&document, pApplications, 1);
    CHECK(cvkJsonStringEquals(cvkGetJsonMember(&document, pApplication, "appId"), "CC1AD845"));
    pTransport = cvkGetJsonMember(&document, pApplication, "transportId");
    CHECK(NULL != pTransport && CVK_JSON_STRING == pTransport->kind && 5 == pTransport->textLength && 0 == memcmp(pTransport->pText, "web-5", 5));
    CHECK(0.75 == cvkGetJsonDouble(cvkGetJsonMember(&document, cvkGetJsonMember(&document, cvkGetJsonMember(&document, &values[0], "status"), "volume"), "level")));
    CHECK(NULL == cvkGetJsonMember(&document, pApplication, "missing"));
    CHECK(NULL == cvkGetJsonElement(&document, pApplications, 2));
    return 0;
}

int main(void)
{
    if (TestEncodeKnownBytes() || TestRoundTripLongPayload() || TestDecodeSkipsUnknownFields() || TestRanges() || TestReceiverStatusJson())
    {
        return 1;
    }
    (void)printf("cast message checks passed\n");
    return 0;
}
