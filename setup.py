"""The C extension so101ik._core: the library in src/ and its binding in python/so101ik/."""
import sys

import numpy
from setuptools import Extension, setup

SOURCES = ["src/kin.c", "src/ik.c", "src/path.c", "src/model.c", "src/sts.c", "src/arm.c", "src/fast.c", "src/sim.c"]

if sys.platform == "win32":   # MSVC: the serial port through hal_win32.c, the 1 ms timer from winmm
    SOURCES.append("src/hal_win32.c")
    ARGS = ["/O2", "/W3", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS"]
    LIBS = ["winmm"]
else:
    SOURCES.append("src/hal_posix.c")
    ARGS = ["-O3", "-Wall", "-Wno-unused-function", "-Wno-missing-field-initializers"]
    LIBS = []

setup(ext_modules=[Extension(
    "so101ik._core",
    sources=["python/so101ik/_core.c", "python/so101ik/_arm.c", "python/so101ik/_path.c"] + SOURCES,
    include_dirs=["include", "src", numpy.get_include()],
    extra_compile_args=ARGS,
    libraries=LIBS,
)])
