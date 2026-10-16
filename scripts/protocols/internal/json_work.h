typedef struct cvkJsonWork cvkJsonWork;

typedef void (*PFN_cvkReadJsonObject)(cvkJsonWork*        pWork,
                                    const cvkJsonValue* pObject,
                                    void*               pOutput);

typedef struct cvkJsonTask
{
    PFN_cvkReadJsonObject read;
    const cvkJsonValue*   pObject;
    void*                pOutput;
} cvkJsonTask;

struct cvkJsonWork
{
    const cvkJsonContext* pContext;
    cvkJsonTask*         pTasks;
    uint64_t             arenaSize;
    uint32_t             capacity;
    uint32_t             count;
    VkBool32             callbackScratch;
};

typedef struct cvkJsonStructureReader
{
    const char*          pName;
    uint32_t             byteCount;
    PFN_cvkReadJsonObject read;
} cvkJsonStructureReader;

/**
 * \brief Reserves bounded parse scratch from callbacks or the unused end of the caller's arena.
 * \param[out] pWork Receives the initialized work stack.
 * \param[in] pContext Document and caller-owned arena.
 * \retval None
 */
void cvkInitializeJsonWork(cvkJsonWork*        pWork,
                           const cvkJsonContext* pContext);

/**
 * \brief Appends one object without invoking another parser.
 * \param[in,out] pWork Initialized work stack.
 * \param[in] read Reader for the object's native structure type.
 * \param[in] pObject JSON object, or NULL for a zeroed structure.
 * \param[out] pOutput Receives the decoded native structure when the work stack runs; must have the layout read expects
 * \retval None
 */
void cvkAppendJsonWork(cvkJsonWork*        pWork,
                       PFN_cvkReadJsonObject read,
                       const cvkJsonValue*   pObject,
                       void*                pOutput);

/**
 * \brief Drains pending objects iteratively and releases the reserved scratch.
 * \param[in,out] pWork Initialized work stack.
 * \retval None
 */
void cvkRunJsonWork(cvkJsonWork* pWork);

/**
 * \brief Allocates an extension structure selected by sType and appends its reader.
 * \param[in,out] pWork Initialized work stack.
 * \param[in] pObject Non-null extension JSON object.
 * \retval void* The allocated native structure.
 */
void* cvkScheduleJsonStructureChain(cvkJsonWork*        pWork,
                                    const cvkJsonValue* pObject);
