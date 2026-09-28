"""Fill a buffer on the GPU using CVK directly. No shader compiler required."""

import ctypes

import cvk

if cvk.initialize() != cvk.VK_SUCCESS:
    raise RuntimeError("Vulkan is unavailable")

instance = cvk.create_instance()
cvk.load_instance(instance)

physical_device = cvk.enumerate_physical_devices(instance)[0]
queue_family = cvk.get_physical_device_compute_queue_family_index(physical_device)
device, queue = cvk.setup_compute_device_queue(physical_device)
cvk.load_device(device)

buffer, memory = cvk.setup_host_visible_buffer(
    physical_device,
    device,
    size=16,
    usage=cvk.VK_BUFFER_USAGE_TRANSFER_DST_BIT,
)

pool, [command] = cvk.setup_primary_command_pool(
    device,
    queue_family_index=queue_family,
)

cvk.begin_one_time_command_buffer(command)
cvk.cmd_fill_buffer(
    command,
    dst_buffer=buffer,
    size=16,
    data=42,
)
cvk.cmd_buffer_barrier(
    command,
    src_access_mask=cvk.VK_ACCESS_TRANSFER_WRITE_BIT,
    dst_access_mask=cvk.VK_ACCESS_HOST_READ_BIT,
    buffer=buffer,
    size=16,
    src_stage_mask=cvk.VK_PIPELINE_STAGE_TRANSFER_BIT,
    dst_stage_mask=cvk.VK_PIPELINE_STAGE_HOST_BIT,
)
cvk.end_command_buffer(command)
cvk.queue_submit_command_buffer_and_wait_idle(queue, command)

address = cvk.map_memory(device, memory, size=16)
numbers = list((ctypes.c_uint32 * 4).from_address(address))
print(numbers)  # [42, 42, 42, 42], written by Vulkan.
cvk.unmap_memory(device, memory)

cvk.destroy_command_pool(device, pool)
cvk.destroy_buffer(device, buffer)
cvk.free_memory(device, memory)
cvk.destroy_device(device)
cvk.destroy_instance(instance)

assert numbers == [42, 42, 42, 42]
