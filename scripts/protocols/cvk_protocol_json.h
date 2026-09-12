/* Tier 3 header fragment (SPEC.md section 8c): the JSON reader's types, folded into cvk.h
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
    uint8_t* pBase;
    size_t   size;
    size_t   used;
} cvkArena;

/* Maps a handle written to JSON as a number (an object id) to a handle; pHandleType names the
** Vulkan handle type ("VkPipelineLayout"). Returns 0 for an unknown id. */
typedef uint64_t (*PFN_cvkResolveJsonHandle)(void* pUser, const char* pHandleType, uint64_t id);

typedef struct cvkJsonContext
{
    cvkJsonDocument          document;
    cvkArena*                pArena;
    PFN_cvkResolveJsonHandle resolveHandle; /* NULL keeps the ids themselves in the handle fields */
    void*                    pUser;
} cvkJsonContext;

/* A Vulkan SC pipeline JSON (the pipeline cache compiler's input), parsed: the objects the
** pipeline needs, each with the id the file refers to it by, and the pipeline itself. Handle
** fields hold the file's ids until cvkCreatePipelineJsonObjects replaces them. */
typedef struct cvkPipelineJson
{
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
