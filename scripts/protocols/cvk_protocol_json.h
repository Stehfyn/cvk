/* Tier 3 header fragment (SPEC.md section 8c): the JSON reader's types, folded into cvk_json.h
** ahead of the declarations. A document is read into a caller-provided value array with no
** allocation; parsed Vulkan structures and their arrays live in a caller-provided arena. */

typedef enum cvkJsonKind
{
    CVK_JSON_NULL,
    CVK_JSON_FALSE,
    CVK_JSON_TRUE,
    CVK_JSON_NUMBER,
    CVK_JSON_STRING,
    CVK_JSON_ARRAY,
    CVK_JSON_OBJECT
} cvkJsonKind;

#define CVK_JSON_NONE (UINT32_MAX)
#define CVK_JSON_ARENA_ALIGNMENT (16)

/* RFC 8259 sections 2 through 7: JSON lexical productions. */
#define CVK_JSON_SPACE (' ')
#define CVK_JSON_TAB ('\t')
#define CVK_JSON_LF ('\n')
#define CVK_JSON_CR ('\r')
#define CVK_JSON_ZERO ('0')
#define CVK_JSON_NINE ('9')
#define CVK_JSON_MINUS ('-')
#define CVK_JSON_PLUS ('+')
#define CVK_JSON_DOT ('.')
#define CVK_JSON_EXPONENT_LOWER ('e')
#define CVK_JSON_EXPONENT_UPPER ('E')
#define CVK_JSON_QUOTE ('"')
#define CVK_JSON_ESCAPE ('\\')
#define CVK_JSON_CONTROL_LIMIT (0x20)
#define CVK_JSON_TRUE_FIRST ('t')
#define CVK_JSON_FALSE_FIRST ('f')
#define CVK_JSON_NULL_FIRST ('n')
#define CVK_JSON_TRUE_TEXT ("true")
#define CVK_JSON_FALSE_TEXT ("false")
#define CVK_JSON_NULL_TEXT ("null")
#define CVK_JSON_OBJECT_OPEN ('{')
#define CVK_JSON_OBJECT_CLOSE ('}')
#define CVK_JSON_ARRAY_OPEN ('[')
#define CVK_JSON_ARRAY_CLOSE (']')
#define CVK_JSON_COLON (':')
#define CVK_JSON_COMMA (',')

/* Numeric conversion buffers include the sign and trailing zero. */
#define CVK_JSON_DECIMAL_BASE (10)
#define CVK_JSON_OCTAL_BASE (8)
#define CVK_JSON_HEXADECIMAL_BASE (16)
#define CVK_JSON_INTEGER_TEXT_CAPACITY (32)
#define CVK_JSON_REAL_TEXT_CAPACITY (64)

typedef struct cvkJsonValue
{
    cvkJsonKind kind;
    const char* pText;       /* body of a number or string, as written (escapes kept) */
    uint32_t    textLength;
    const char* pKey;        /* member name when the value is an object member, else NULL */
    uint32_t    keyLength;
    uint32_t    childCount;  /* elements of an array, members of an object */
    uint32_t    firstChild;  /* index into the document's values, or CVK_JSON_NONE */
    uint32_t    nextSibling; /* index of the next element or member, or CVK_JSON_NONE */
} cvkJsonValue;

typedef struct cvkJsonDocument
{
    const cvkJsonValue* pValues; /* value 0 is the root */
    uint32_t            valueCount;
} cvkJsonDocument;

typedef struct cvkJsonReader
{
    const char*   pCursor;
    const char*   pEnd;
    cvkJsonValue* pValues;
    uint32_t      valueCapacity;
    uint32_t      valueCount;
} cvkJsonReader;

typedef struct cvkJsonName
{
    const char* pName;
    uint64_t    value;
} cvkJsonName;

typedef struct cvkArena
{
    uint8_t*                     pBase;
    size_t                       size;
    size_t                       used;
    const VkAllocationCallbacks* pAllocator;
    struct cvkArenaBlock*        pBlocks;
} cvkArena;

/* Writes a native handle of the named type to pHandle. The caller initializes
** that typed object to VK_NULL_HANDLE. Failure leaves it unchanged. */
typedef VkBool32 (*PFN_cvkResolveJsonHandle)(void*       pUser,
                                          const char* pHandleType,
                                          uint64_t    id,
                                          uint32_t    handleByteCount,
                                          void*       pHandle);

/* Numeric IDs are metadata, never pointer-valued Vulkan handles. The referenced
** output objects must remain at their parse-time addresses while this list is used. */
typedef struct cvkJsonHandleReference
{
    struct cvkJsonHandleReference* pNext;
    const char*                   pHandleType;
    const void*                   pHandle;
    uint64_t                      id;
} cvkJsonHandleReference;

typedef struct cvkJsonContext
{
    cvkJsonDocument          document;
    cvkArena*                pArena;
    PFN_cvkResolveJsonHandle resolveHandle;
    void*                    pUser;
    cvkJsonHandleReference** ppHandleReferences; /* required for deferred resolution */
} cvkJsonContext;

/* A Vulkan SC pipeline JSON (the pipeline cache compiler's input), parsed: the objects the
** pipeline needs, each with the id the file refers to it by, and the pipeline itself. Handle
** fields hold native handles. Numeric IDs live in pHandleReferences. */
typedef struct cvkPipelineJson
{
    cvkJsonHandleReference*             pHandleReferences;
    VkPipelineBindPoint                 bindPoint;
    VkGraphicsPipelineCreateInfo        graphicsPipelineCreateInfo;
    VkComputePipelineCreateInfo         computePipelineCreateInfo;
    VkBool32                            hasPipelineLayout;
    VkPipelineLayoutCreateInfo          pipelineLayoutCreateInfo;
    uint32_t                            renderPassVersion; /* 0 none, 1 VkRenderPassCreateInfo, 2 VkRenderPassCreateInfo2 */
    VkRenderPassCreateInfo              renderPassCreateInfo;
    VkRenderPassCreateInfo2             renderPassCreateInfo2;
    uint32_t                            descriptorSetLayoutCount;
    uint64_t*                           pDescriptorSetLayoutIds;
    VkDescriptorSetLayoutCreateInfo*    pDescriptorSetLayoutCreateInfos;
    uint32_t                            samplerCount;
    uint64_t*                           pSamplerIds;
    VkSamplerCreateInfo*                pSamplerCreateInfos;
    uint32_t                            samplerYcbcrConversionCount;
    uint64_t*                           pSamplerYcbcrConversionIds;
    VkSamplerYcbcrConversionCreateInfo* pSamplerYcbcrConversionCreateInfos;
    uint32_t                            shaderCount;
    VkShaderStageFlagBits*              pShaderStages;
    const char**                        ppShaderFileNames;
    uint32_t                            enabledExtensionCount;
    const char**                        ppEnabledExtensionNames;
    VkBool32                            hasPipelineUuid;
    uint8_t                             pipelineUuid[VK_UUID_SIZE];
} cvkPipelineJson;

/* The handles created from a cvkPipelineJson, each beside the id the file used for it. */
typedef struct cvkPipelineJsonObjects
{
    uint32_t                 samplerYcbcrConversionCount;
    uint64_t                 samplerYcbcrConversionIds[CVK_MAX_PIPELINE_JSON_OBJECTS];
    VkSamplerYcbcrConversion samplerYcbcrConversions[CVK_MAX_PIPELINE_JSON_OBJECTS];
    uint32_t                 samplerCount;
    uint64_t                 samplerIds[CVK_MAX_PIPELINE_JSON_OBJECTS];
    VkSampler                samplers[CVK_MAX_PIPELINE_JSON_OBJECTS];
    uint32_t                 descriptorSetLayoutCount;
    uint64_t                 descriptorSetLayoutIds[CVK_MAX_PIPELINE_JSON_OBJECTS];
    VkDescriptorSetLayout    descriptorSetLayouts[CVK_MAX_PIPELINE_JSON_OBJECTS];
    VkPipelineLayout         pipelineLayout;
    VkRenderPass             renderPass;
    VkPipeline               pipeline;
} cvkPipelineJsonObjects;
