"""Mechanical C-to-Python argument conversion; no Vulkan policy or resource ownership."""

import ctypes as ct
import inspect

from . import raw
from . import _generated as generated


def convert(value, native_type):
    if native_type is ct.c_char_p and isinstance(value, str):
        return value.encode("utf-8")
    if isinstance(value, (list, tuple)):
        if issubclass(native_type, ct._Pointer):
            element = native_type._type_
            return (element * len(value))(*(convert(item, element) for item in value))
        if issubclass(native_type, (ct.Structure, ct.Union)):
            return native_type(*value)
    return value


def unpack(value):
    if isinstance(value, ct._SimpleCData):
        return value.value
    return value


def bind(name, metadata):
    c_name, parameters, outputs, counts, defaults = metadata
    native = getattr(raw, c_name)
    _, result_type, types, _, indirect = generated.FUNCTIONS[c_name]
    required = []
    optional = []
    for index, parameter in enumerate(parameters):
        if index in outputs:
            continue
        if index in defaults:
            optional.append(
                inspect.Parameter(
                    parameter,
                    inspect.Parameter.KEYWORD_ONLY,
                    default=defaults[index],
                )
            )
        else:
            required.append(
                inspect.Parameter(
                    parameter,
                    inspect.Parameter.POSITIONAL_OR_KEYWORD,
                )
            )
    signature = inspect.Signature(required + optional)

    def function(*args, **kwargs):
        bound = signature.bind(*args, **kwargs)
        bound.apply_defaults()
        values = [None] * len(parameters)
        for index, parameter in enumerate(parameters):
            if index not in outputs:
                values[index] = bound.arguments[parameter]
        for count_index, array_indices in counts.items():
            arrays = [values[index] for index in array_indices]
            array = next((value for value in arrays if value is not None), None)
            if values[count_index] is None:
                if array is None:
                    values[count_index] = 0
                elif isinstance(array, (list, tuple, ct.Array)):
                    values[count_index] = len(array)
                else:
                    raise TypeError(
                        f"Specify {parameters[count_index]} for a raw pointer"
                    )
            for array_index in array_indices:
                array = values[array_index]
                if isinstance(array, (list, tuple, ct.Array)) and values[
                    count_index
                ] > len(array):
                    raise ValueError(
                        f"{parameters[count_index]} exceeds {parameters[array_index]} length"
                    )
            if all(value is None for value in arrays) and values[count_index]:
                raise ValueError(
                    f"An array is required for a nonzero {parameters[count_index]}"
                )
        for index, specification in outputs.items():
            capacity, count_index = specification
            element = types[index]._type_
            if capacity is not None:
                values[index] = (element * capacity)()
            elif count_index is not None:
                count = values[count_index]
                if not isinstance(count, int) or count < 0:
                    raise ValueError(f"{parameters[count_index]} must be nonnegative")
                values[index] = (element * count)()
            else:
                values[index] = element()
        for index, value in enumerate(values):
            if index not in outputs:
                native_type = types[index]._type_ if index in indirect else types[index]
                values[index] = convert(value, native_type)
        result = native(*values)
        returned = [] if result_type is None else [result]
        consumed_counts = {
            count_index
            for capacity, count_index in outputs.values()
            if capacity is not None and count_index in outputs
        }
        for index, (capacity, count_index) in outputs.items():
            if index in consumed_counts:
                continue
            value = values[index]
            if capacity is not None or count_index is not None:
                value._cvk_inputs = [
                    item
                    for position, item in enumerate(values)
                    if position not in outputs
                ]
                length = (
                    unpack(values[count_index])
                    if count_index is not None
                    else len(value)
                )
                if not 0 <= length <= len(value):
                    raise RuntimeError(
                        "CVK returned a count outside the allocated array"
                    )
                value = [unpack(item) for item in value[:length]]
            elif isinstance(value, (ct.Structure, ct.Union)):
                # Native builders store borrowed pointers; retain Python input owners.
                value._cvk_inputs = [
                    item
                    for position, item in enumerate(values)
                    if position not in outputs
                ]
            else:
                value = unpack(value)
            returned.append(value)
        if not returned:
            return None
        if len(returned) == 1:
            return returned[0]
        return tuple(returned)

    function.__name__ = name
    function.__signature__ = signature
    function.__doc__ = (
        f"{name}{signature}\nGenerated from {c_name}; C outputs are returned in order."
    )
    return function
