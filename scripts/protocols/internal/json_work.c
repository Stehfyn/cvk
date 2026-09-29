/*************************************************************************************************/
/**
 * \par Details:
 *  Each pending structure belongs to a distinct document value. One task per
 *  value therefore bounds the stack without recursion or heap allocation.
 *  Permanent output grows from the front of the arena; scratch occupies its tail.
 */
void cvkInitializeJsonWork(cvkJsonWork*          pWork,
                           const cvkJsonContext* pContext)
{
    uint64_t scratch_size;
    uint64_t scratch_start;
    uint64_t misalignment;
    uint32_t capacity;
    cvkArena scratch;

    CVK_ASSERT(NULL != pWork);
    CVK_ASSERT(NULL != pContext);
    CVK_ASSERT(NULL != pContext->pArena);
    CVK_ASSERT(NULL != pContext->pArena->pBase);
    CVK_ASSERT(pContext->pArena->used <= pContext->pArena->size);
    capacity      = pContext->document.valueCount;
    capacity      = (0 < capacity) ? capacity : 1;
    scratch_size  = (uint64_t)capacity * sizeof(cvkJsonTask);
    CVK_ASSERT(scratch_size <= pContext->pArena->size);
    scratch_start = pContext->pArena->size - scratch_size;
    misalignment  = (uintptr_t)(pContext->pArena->pBase + scratch_start) % CVK_JSON_ARENA_ALIGNMENT;
    CVK_ASSERT(misalignment <= scratch_start);
    scratch_start -= misalignment;
    CVK_ASSERT(pContext->pArena->used <= scratch_start);
    scratch       = *pContext->pArena;
    scratch.used  = scratch_start;
    pWork->pContext = pContext;
    pWork->pTasks   = (cvkJsonTask*)cvkAllocateArena(&scratch,
                                                  scratch_size,
                                                  CVK_JSON_ARENA_ALIGNMENT);
    pWork->arenaSize = pContext->pArena->size;
    pWork->capacity = capacity;
    pWork->count    = 0;
    pContext->pArena->size = scratch_start;
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
    pWork->pContext->pArena->size = pWork->arenaSize;
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
