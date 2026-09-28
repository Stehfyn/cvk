"""Generated, procedural Python bindings for CVK."""

from . import raw
from . import _generated
from ._binding import bind as _bind

__version__ = "0.1.0"


def __getattr__(name):
    if name in _generated.PYTHON_FUNCTIONS:
        function = _bind(name, _generated.PYTHON_FUNCTIONS[name])
        globals()[name] = function
        return function
    return getattr(raw, name)


def __dir__():
    return sorted(set(globals()) | set(_generated.PYTHON_FUNCTIONS) | set(dir(raw)))
