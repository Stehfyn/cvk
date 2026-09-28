/* Python owns the lifetime; CVK owns all Vulkan operations. */
#include <volk.h>

VkResult cvkPythonInitialize(void)
{
    return volkInitialize();
}

void cvkPythonLoadInstance(VkInstance instance)
{
    volkLoadInstance(instance);
}

void cvkPythonLoadDevice(VkDevice device)
{
    volkLoadDevice(device);
}
