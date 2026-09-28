"""Check generated argument/return conversion and the underlying native ABI."""

import ctypes as ct
import gc
import inspect
import os
from pathlib import Path
import runpy
import unittest

import cvk
from cvk import _generated as g
from cvk import raw as v


class BindingsTests(unittest.TestCase):
    def test_every_function_has_a_native_symbol_and_python_signature(self):
        self.assertGreater(len(g.FUNCTIONS), 5000)
        for name in g.FUNCTIONS:
            with self.subTest(name=name):
                self.assertTrue(callable(getattr(v, name)))
        for name in g.PYTHON_FUNCTIONS:
            with self.subTest(name=name):
                self.assertIsInstance(
                    inspect.signature(getattr(cvk, name)), inspect.Signature
                )

    def test_64_bit_scalars_and_returned_struct(self):
        value = (1 << 48) + 1
        self.assertEqual(cvk.VK_WHOLE_SIZE, (1 << 64) - 1)
        self.assertEqual(cvk.CVK_JSON_NONE, (1 << 32) - 1)
        self.assertEqual(cvk.align_device_size(value, 256), (1 << 48) + 256)
        copy = cvk.init_buffer_copy(
            src_offset=value,
            dst_offset=value + 1,
            size=value + 2,
        )
        self.assertEqual(copy.srcOffset, value)
        self.assertEqual(copy.dstOffset, value + 1)
        self.assertEqual(copy.size, value + 2)

    def test_struct_and_union_by_value(self):
        info = cvk.init_image_create_info(
            image_type=cvk.VK_IMAGE_TYPE_2D,
            format=cvk.VK_FORMAT_R8G8B8A8_UNORM,
            extent=(17, 19, 1),
            mip_levels=1,
            array_layers=1,
            samples=cvk.VK_SAMPLE_COUNT_1_BIT,
            tiling=cvk.VK_IMAGE_TILING_OPTIMAL,
            usage=cvk.VK_IMAGE_USAGE_STORAGE_BIT,
            initial_layout=cvk.VK_IMAGE_LAYOUT_UNDEFINED,
        )
        self.assertEqual(info.sType, cvk.VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)
        self.assertEqual(info.extent.width, 17)
        clear = cvk.VkClearValue()
        clear.color.float32[:] = [0.25, 0.5, 0.75, 1.0]
        attachment = cvk.init_clear_attachment(
            aspect_mask=cvk.VK_IMAGE_ASPECT_COLOR_BIT,
            color_attachment=2,
            clear_value=clear,
        )
        self.assertEqual(
            list(attachment.clearValue.color.float32), [0.25, 0.5, 0.75, 1.0]
        )

    def test_strings_and_arrays_retain_their_storage(self):
        info = cvk.init_instance_create_info(
            enabled_layer_names=["example layer"],
        )
        gc.collect()
        self.assertEqual(info.enabledLayerCount, 1)
        self.assertEqual(info.ppEnabledLayerNames[0], b"example layer")
        with self.assertRaises(ValueError):
            cvk.init_instance_create_info(
                enabled_layer_count=2,
                enabled_layer_names=["only one"],
            )

    def test_defaults_remove_boilerplate_and_output_pointers(self):
        signature = inspect.signature(cvk.setup_host_visible_buffer)
        required = [
            name
            for name, parameter in signature.parameters.items()
            if parameter.default is inspect.Parameter.empty
        ]
        self.assertEqual(required, ["physical_device", "device", "size", "usage"])
        self.assertNotIn("buffer", signature.parameters)
        self.assertNotIn("memory", signature.parameters)
        stages = inspect.signature(cvk.init_descriptor_set_layout_binding)
        self.assertIs(stages.parameters["stage_flags"].default, inspect.Parameter.empty)
        with self.assertRaises(TypeError):
            cvk.align_device_size(1)

    def test_raw_json_arrays_and_uint64(self):
        text = b"[18446744073709551615, 42]"
        values = (v.cvkJsonValue * 8)()
        count = v.cvkParseJsonDocument(
            pText=text,
            length=len(text),
            pValues=values,
            valueCapacity=len(values),
        )
        self.assertEqual(count, 3)
        self.assertEqual(v.cvkParseJsonUint64(values[1]), (1 << 64) - 1)
        self.assertEqual(v.cvkParseJsonUint64(values[2]), 42)

    def test_callback_typedef(self):
        callback = v.PFN_cvkResolveJsonHandle(lambda user, kind, number: number + 1)
        self.assertEqual(callback(None, b"VkBuffer", 1 << 48), (1 << 48) + 1)

    @unittest.skipUnless(
        os.environ.get("CVK_TEST_GPU") == "1", "set CVK_TEST_GPU=1 for Vulkan"
    )
    def test_procedural_gpu_example(self):
        path = Path(__file__).resolve().parents[1] / "examples/python/fill_buffer.py"
        runpy.run_path(str(path))


if __name__ == "__main__":
    unittest.main()
