/*************************************************************************************************/
/**
 * \par Details:
 *  Each pending structure belongs to a distinct document value. One task per
 *  value therefore bounds the stack without recursion. Callback-backed arenas
 *  allocate the stack through those callbacks; caller-owned arenas use their tail.
 */
void cvkInitializeJsonWork(cvkJsonWork*          pWork,
                           const cvkJsonContext* pContext)
{
    uint64_t scratch_size;
    uint64_t safeScratchSize;
    uint64_t scratch_start;
    uint64_t misalignment;
    uint64_t safeMisalignment;
    VkBool32 validMisalignment;
    VkBool32 fitsScratch;
    uint32_t capacity;
    cvkArena scratch;

    CVK_ASSERT(NULL != pWork);
    CVK_ASSERT(NULL != pContext);
    CVK_ASSERT(NULL != pContext->pArena);
    CVK_ASSERT(pContext->pArena->used <= pContext->pArena->size);
    capacity      = pContext->document.valueCount;
    capacity      = (0 < capacity) ? capacity : 1;
    scratch_size  = (uint64_t)capacity * sizeof(cvkJsonTask);
    pWork->pContext = pContext;
    pWork->arenaSize = pContext->pArena->size;
    pWork->capacity = capacity;
    pWork->count    = 0;
    pWork->callbackScratch = VK_FALSE;
    if (NULL != pContext->pArena->pAllocator)
    {
        const VkAllocationCallbacks* pAllocator = pContext->pArena->pAllocator;

        pWork->pTasks = (cvkJsonTask*)pAllocator->pfnAllocation(pAllocator->pUserData,
                                                                scratch_size,
                                                                CVK_JSON_ARENA_ALIGNMENT,
                                                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        CVK_ASSERT(NULL != pWork->pTasks);
        pWork->callbackScratch = VK_TRUE;
    }
    else
    {
        CVK_ASSERT(NULL != pContext->pArena->pBase);
        fitsScratch = scratch_size <= pContext->pArena->size;
        safeScratchSize = fitsScratch ? scratch_size : 0;
        scratch_start = pContext->pArena->size - safeScratchSize;
        misalignment  = (uintptr_t)(pContext->pArena->pBase + scratch_start) % CVK_JSON_ARENA_ALIGNMENT;
        validMisalignment = misalignment <= scratch_start;
        safeMisalignment = validMisalignment ? misalignment : 0;
        scratch_start -= safeMisalignment;
        scratch       = *pContext->pArena;
        scratch.used  = scratch_start;
        pWork->pTasks = (cvkJsonTask*)cvkAllocateArena(&scratch, safeScratchSize, CVK_JSON_ARENA_ALIGNMENT);
        CVK_ASSERT(fitsScratch);
        CVK_ASSERT(validMisalignment);
        CVK_ASSERT(pContext->pArena->used <= scratch_start);
        pContext->pArena->size = scratch_start;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void cvkAppendJsonWork(cvkJsonWork*          pWork,
                       PFN_cvkReadJsonObject read,
                       const cvkJsonValue*   pObject,
                       void*                pOutput)
{
    cvkJsonTask* task;

    CVK_ASSERT(NULL != pWork);
    CVK_ASSERT(NULL != read);
    CVK_ASSERT(NULL != pOutput);
    CVK_ASSERT(pWork->count < pWork->capacity);
    task          = &pWork->pTasks[pWork->count];
    task->read    = read;
    task->pObject = pObject;
    task->pOutput = pOutput;
    ++pWork->count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Readers may append child objects, but never invoke this drain operation.
 *  The native call stack therefore does not grow with JSON nesting depth.
 */
void cvkRunJsonWork(cvkJsonWork* pWork)
{
    cvkJsonTask task;

    CVK_ASSERT(NULL != pWork);
    for (; 0 < pWork->count;)
    {
        --pWork->count;
        task = pWork->pTasks[pWork->count];
        task.read(pWork, task.pObject, task.pOutput);
    }
    if (pWork->callbackScratch)
    {
        const VkAllocationCallbacks* pAllocator = pWork->pContext->pArena->pAllocator;

        pAllocator->pfnFree(pAllocator->pUserData, pWork->pTasks);
    }
    else
    {
        pWork->pContext->pArena->size = pWork->arenaSize;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The dispatch table is derived from the profile's registry structures.
 */
void* cvkScheduleJsonStructureChain(cvkJsonWork*        pWork,
                                    const cvkJsonValue* pObject)
{
    static const cvkJsonStructureReader readers[] =
    {
@STRUCTURE_READERS@
    };
    const cvkJsonValue* structure_type;
    void*              storage;
    uint32_t           index;

    storage        = NULL;
    structure_type = cvkGetJsonMember(&pWork->pContext->document, pObject, "sType");
    for (index = 0; index < VK_COUNT_OF(readers); ++index)
    {
        if (cvkJsonStringEquals(structure_type, readers[index].pName))
        {
            storage = cvkAllocateArena(pWork->pContext->pArena,
                                       readers[index].byteCount,
                                       CVK_JSON_ARENA_ALIGNMENT);
            cvkAppendJsonWork(pWork, readers[index].read, pObject, storage);
            break;
        }
    }
    CVK_ASSERT(NULL != storage);
    return storage;
}
