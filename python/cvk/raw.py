"""Generated CVK functions, Vulkan ctypes, and constants with the C names intact.

Output pointers and arrays are explicit here, exactly as in cvk.h. Vulkan errors
retain CVK's fatal policy. Initialize/load Volk before making raw Vulkan calls.
"""

import ctypes as ct
import inspect
import os
from pathlib import Path

from . import _generated as _g

_library = None


def _load():
    global _library
    if _library is None:
        override = os.environ.get("CVK_LIBRARY")
        candidates = (
            [Path(override)]
            if override
            else [
                Path(__file__).with_name("_cvk" + suffix)
                for suffix in (".dll", ".so", ".dylib")
            ]
        )
        path = next((p for p in candidates if p.is_file()), None)
        if path is None:
            raise ImportError(
                "CVK native library is missing. Build with CVK_BUILD_PYTHON=ON "
                "or install with 'python -m pip install .'."
            )
        _library = ct.CDLL(str(path.resolve()))
    return _library


def _function(name, symbol, result, types, names, indirect):
    native = getattr(_load(), symbol)
    native.restype, native.argtypes = result, types
    signature = inspect.Signature(
        [inspect.Parameter(p, inspect.Parameter.POSITIONAL_OR_KEYWORD) for p in names]
    )

    def call(*args, **kwargs):
        if kwargs:
            bound = signature.bind(*args, **kwargs)
            args = bound.args
        if len(args) != len(types):
            raise TypeError(
                f"{name}{signature} requires {len(types)} arguments, got {len(args)}"
            )
        if indirect:
            args = list(args)
            for index in indirect:
                expected = types[index]._type_
                if not isinstance(args[index], expected):
                    raise TypeError(f"{names[index]} must be {expected.__name__}")
                args[index] = ct.byref(args[index])
        return native(*args)

    call.__name__ = name
    call.__signature__ = signature
    call.__doc__ = (
        f"{name}{signature}\nGenerated binding; outputs use explicit ctypes pointers."
    )
    call.argtypes, call.restype = types, result
    return call


def __getattr__(name):
    if name in _g.FUNCTIONS:
        value = _function(name, *_g.FUNCTIONS[name])
    elif name in ("initialize", "load_instance", "load_device"):
        symbol, result, types, names = {
            "initialize": ("cvkPythonInitialize", ct.c_int, [], []),
            "load_instance": (
                "cvkPythonLoadInstance",
                None,
                [ct.c_void_p],
                ["instance"],
            ),
            "load_device": ("cvkPythonLoadDevice", None, [ct.c_void_p], ["device"]),
        }[name]
        value = _function(name, symbol, result, types, names, [])
    elif hasattr(_g, name) and not name.startswith("_"):
        value = getattr(_g, name)
    else:
        raise AttributeError(name)
    globals()[name] = value
    return value


def __dir__():
    return sorted(
        set(globals())
        | set(_g.FUNCTIONS)
        | {n for n in vars(_g) if not n.startswith("_")}
    )
