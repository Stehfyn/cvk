/* One zero-initialized cache per command and dispatch scope, private to each thread.
 * A cache retains the most recently used handle. CVK_CALL returns a typed pointer.
 * Destroy wrappers invalidate all threads' caches. If destroying handles outside
 * cvk, call cvkInvalidateCallCache before using a replacement handle. */
#if defined(__cplusplus)
#define CVK_THREAD_LOCAL thread_local
#elif defined(_MSC_VER)
#define CVK_THREAD_LOCAL __declspec(thread)
#else
#define CVK_THREAD_LOCAL _Thread_local
#endif

typedef struct cvkCallCache
{
    uintptr_t          handle;
    uint64_t           generation;
    PFN_vkVoidFunction function;
} cvkCallCache;

void cvkInvalidateCallCache(void);
PFN_vkVoidFunction cvkGetCachedDeviceProcAddr(cvkCallCache* pCache, VkDevice device, const char* pName);
PFN_vkVoidFunction cvkGetCachedInstanceProcAddr(cvkCallCache* pCache, VkInstance instance, const char* pName);

#define CVK_CALL(cache, scope, handle, command) ((PFN_##command)cvkGetCached##scope##ProcAddr((cache), (handle), #command))
