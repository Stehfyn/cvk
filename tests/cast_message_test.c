/* CASTV2 framing, HTTP range parsing and receiver-status JSON checks; no network, no Vulkan. */
#include "utl/utl_castv2.h"
#include "cvk/cvk_json.h"
#include "media_server.h"
#include "mp4_index.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                                                                                                                       \
    for (;;)                                                                                                                                                                                           \
    {                                                                                                                                                                                                  \
        if (!(x))                                                                                                                                                                                      \
        {                                                                                                                                                                                              \
            (void)fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                                                                                                      \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
        break;                                                                                                                                                                                         \
    }

static int TestEncodeKnownBytes(void)
{
    /* 08 00 | 12 01 's' | 1A 01 'd' | 22 01 'n' | 28 00 | 32 02 '{' '}'  = 17 bytes */
    static const byte_t expected[] = {0, 0, 0, 17, 0x08, 0x00, 0x12, 0x01, 's', 0x1A, 0x01, 'd', 0x22, 0x01, 'n', 0x28, 0x00, 0x32, 0x02, '{', '}'};
    Utl_CastV2_Message_t message;
    byte_t       frame[64];
    uint32_t      length;

    message.source.data = (const byte_t*)"s";
    message.source.byteCount = sizeof("s") - 1;
    message.destination.data = (const byte_t*)"d";
    message.destination.byteCount = sizeof("d") - 1;
    message.nameSpace.data = (const byte_t*)"n";
    message.nameSpace.byteCount = sizeof("n") - 1;
    message.payload.data = (const byte_t*)"{}";
    message.payload.byteCount = sizeof("{}") - 1;
    message.payloadType = UTL_CASTV2_STRING;
    length = Utl_CastV2_WriteFrame(&message, frame, sizeof(frame));
    CHECK(length == sizeof(expected));
    CHECK(0 == memcmp(frame, expected, sizeof(expected)));
    CHECK(0 == Utl_CastV2_WriteFrame(&message, frame, sizeof(expected) - 1));
    return 0;
}

static int TestRoundTripLongPayload(void)
{
    static byte_t frame[UTL_CASTV2_MAX_FRAME];
    byte_t          payload[300];
    Utl_CastV2_Message_t message;
    Utl_CastV2_Message_t decoded;
    uint32_t      length;
    uint32_t      index;

    for (index = 0; index < sizeof(payload) - 1; ++index)
    {
        payload[index] = (byte_t)('a' + (index % 26));
    }
    payload[sizeof(payload) - 1] = 0;
    message.source.data = (const byte_t*)"sender-0";
    message.source.byteCount = sizeof("sender-0") - 1;
    message.destination.data = (const byte_t*)"receiver-0";
    message.destination.byteCount = sizeof("receiver-0") - 1;
    message.nameSpace.data = (const byte_t*)"urn:x-cast:com.google.cast.receiver";
    message.nameSpace.byteCount = sizeof("urn:x-cast:com.google.cast.receiver") - 1;
    message.payload.data = payload;
    message.payload.byteCount = sizeof(payload) - 1;
    message.payloadType = UTL_CASTV2_STRING;
    length = Utl_CastV2_WriteFrame(&message, frame, sizeof(frame));
    CHECK(length > 4);
    CHECK((((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16) | ((uint32_t)frame[2] << 8) | frame[3]) == length - 4);
    CHECK(Utl_CastV2_ReadMessage(&decoded, frame + 4, length - 4));
    CHECK(decoded.source.byteCount == 8 && 0 == memcmp(decoded.source.data, "sender-0", 8));
    CHECK(decoded.destination.byteCount == 10 && 0 == memcmp(decoded.destination.data, "receiver-0", 10));
    CHECK(sizeof("urn:x-cast:com.google.cast.receiver") - 1 == decoded.nameSpace.byteCount);
    CHECK(0 == memcmp(decoded.nameSpace.data, "urn:x-cast:com.google.cast.receiver", decoded.nameSpace.byteCount));
    CHECK(sizeof("urn:x-cast:com.google.cast.media") - 1 != decoded.nameSpace.byteCount);
    CHECK(decoded.payload.byteCount == sizeof(payload) - 1 && 0 == memcmp(decoded.payload.data, payload, sizeof(payload) - 1));
    CHECK(UTL_CASTV2_STRING == decoded.payloadType);
    /* truncated bodies are rejected */
    CHECK(!Utl_CastV2_ReadMessage(&decoded, frame + 4, length - 5));
    return 0;
}

static int TestDecodeSkipsUnknownFields(void)
{
    /* protocol_version, source, destination, unknown fixed64 (field 9), unknown fixed32 (field 10),
    ** namespace, payload_type BINARY, payload_binary */
    static const byte_t body[] = {0x08, 0x00, 0x12, 0x01, 'a', 0x1A, 0x01, 'b', 0x49, 1, 2, 3, 4, 5, 6, 7, 8, 0x55, 1, 2, 3, 4, 0x22, 0x01, 'n', 0x28, 0x01, 0x3A, 0x02, 0xFF, 0x00};
    Utl_CastV2_Message_t decoded;

    CHECK(Utl_CastV2_ReadMessage(&decoded, body, sizeof(body)));
    CHECK(UTL_CASTV2_BINARY == decoded.payloadType);
    CHECK(decoded.payload.byteCount == 2 && 0xFF == (byte_t)decoded.payload.data[0]);
    CHECK(1 == decoded.nameSpace.byteCount);
    CHECK(0 == memcmp(decoded.nameSpace.data, "n", 1));
    /* a different protocol version is not understood */
    {
        static const byte_t wrongVersion[] = {0x08, 0x01, 0x12, 0x01, 'a', 0x1A, 0x01, 'b', 0x22, 0x01, 'n', 0x32, 0x00};

        CHECK(!Utl_CastV2_ReadMessage(&decoded, wrongVersion, sizeof(wrongVersion)));
    }
    /* a body missing the namespace is incomplete */
    {
        static const byte_t noNamespace[] = {0x08, 0x00, 0x12, 0x01, 'a', 0x1A, 0x01, 'b', 0x32, 0x00};

        CHECK(!Utl_CastV2_ReadMessage(&decoded, noNamespace, sizeof(noNamespace)));
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
    CHECK(0 == MediaServer_ParseRange("bytes=-1", 0, &first, &last)); /* Empty representations ignore Range. */
    CHECK(-1 == MediaServer_ParseRange("bytes=-0", 1000, &first, &last));
    CHECK(1 == MediaServer_ParseRange("bytes=1000-,10-20", 1000, &first, &last) && 10 == first && 20 == last);
    CHECK(0 == MediaServer_ParseRange("bytes=10-20,5-2", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=10-20junk", 1000, &first, &last));
    CHECK(1 == MediaServer_ParseRange("bytes=0-18446744073709551615", 1000, &first, &last) && 0 == first && 999 == last);
    CHECK(0 == MediaServer_ParseRange("bytes=0-18446744073709551616", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("items=0-1", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=5-2", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=-", 1000, &first, &last));
    CHECK(0 == MediaServer_ParseRange("bytes=a-b", 1000, &first, &last));
    CHECK(0 == strcmp("video/mp4", MediaServer_GetContentTypeForExtension(L".MP4")));
    CHECK(0 == strcmp("video/x-matroska", MediaServer_GetContentTypeForExtension(L".mkv")));
    CHECK(0 == strcmp("application/octet-stream", MediaServer_GetContentTypeForExtension(L".bin")));
    CHECK(0 == strcmp("application/octet-stream", MediaServer_GetContentTypeForExtension(NULL)));
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

static int TestKeyframeSnap(void)
{
    static const double keyframes[] = {0.0, 8.333, 16.667, 25.0};

    CHECK(0.0 == Mp4Index_SnapToKeyframe(keyframes, 4, 0.0));
    CHECK(0.0 == Mp4Index_SnapToKeyframe(keyframes, 4, 8.3));
    CHECK(8.333 == Mp4Index_SnapToKeyframe(keyframes, 4, 8.333));
    CHECK(16.667 == Mp4Index_SnapToKeyframe(keyframes, 4, 24.9));
    CHECK(25.0 == Mp4Index_SnapToKeyframe(keyframes, 4, 500.0));
    CHECK(-1.0 == Mp4Index_SnapToKeyframe(keyframes, 4, -1.0)); /* before the first keyframe: unchanged */
    CHECK(12.5 == Mp4Index_SnapToKeyframe(keyframes, 0, 12.5)); /* no index: unchanged */
    CHECK(0 == Mp4Index_ReadKeyframes(L"C:\\does\\not\\exist.mp4", NULL, 0));
    return 0;
}

int main(void)
{
    if (TestEncodeKnownBytes() || TestRoundTripLongPayload() || TestDecodeSkipsUnknownFields() || TestRanges() || TestReceiverStatusJson() || TestKeyframeSnap())
    {
        return 1;
    }
    (void)printf("cast message checks passed\n");
    return 0;
}
