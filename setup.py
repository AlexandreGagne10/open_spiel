# Copyright 2019 DeepMind Technologies Limited
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""The setup script for setuptools.

See https://setuptools.readthedocs.io/en/latest/setuptools.html
"""

import os
import shutil
import subprocess
import sys

import setuptools
from setuptools.command.build_ext import build_ext


class CMakeExtension(setuptools.Extension):
  """An extension with no sources.

  We do not want distutils to handle any of the compilation (instead we rely
  on CMake), so we always pass an empty list to the constructor.
  """

  def __init__(self, name, sourcedir=""):
    super().__init__(name, sources=[])
    self.sourcedir = os.path.abspath(sourcedir)


class BuildExt(build_ext):
  """Our custom build_ext command.

  Uses CMake to build extensions instead of a bare compiler (e.g. gcc, clang).
  """

  def run(self):
    self._check_build_environment()
    for ext in self.extensions:
      self.build_extension(ext)

  def _check_build_environment(self):
    """Check for required build tools: CMake, C++ compiler, and python dev."""
    try:
      subprocess.check_call(["cmake", "--version"])
    except OSError as e:
      ext_names = ", ".join(e.name for e in self.extensions)
      raise RuntimeError(
          "CMake must be installed to build" +
          f"the following extensions: {ext_names}") from e
    print("Found CMake")

    cxx = self._select_cxx()
    if cxx is None:
      ext_names = ", ".join(ext.name for ext in self.extensions)
      raise RuntimeError(
          "No compatible C++ compiler was detected to build the "
          f"following extensions: {ext_names}."
          " On Windows, ensure that cl.exe or clang-cl.exe is available in a "
          "Developer Command Prompt or set the CXX environment variable."
      )
    try:
      self._check_compiler_version(cxx)
    except (OSError, subprocess.CalledProcessError) as e:
      ext_names = ", ".join(ext.name for ext in self.extensions)
      raise RuntimeError(
          "A C++ compiler that supports c++17 must be installed to build the "
          + "following extensions: {}".format(ext_names)
          + ". We recommend: Clang version >= 7.0.0 or Microsoft Visual Studio "
          + "Build Tools."
      ) from e
    print("Found C++ compiler: {}".format(cxx))
    self._resolved_cxx = cxx

  def build_extension(self, ext):
    extension_dir = os.path.abspath(
        os.path.dirname(self.get_ext_fullpath(ext.name)))
    cxx = self._select_cxx()
    env = os.environ.copy()
    cmake_args = [
        f"-DPython3_EXECUTABLE={sys.executable}",
        f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extension_dir}",
    ]
    if cxx is not None:
      cmake_args.append(f"-DCMAKE_CXX_COMPILER={cxx}")
    cfg = "Debug" if self.debug else "Release"
    if not sys.platform.startswith("win"):
      cmake_args.append(f"-DCMAKE_BUILD_TYPE={cfg}")
    if "CMAKE_BUILD_PARALLEL_LEVEL" not in env:
      env["CMAKE_BUILD_PARALLEL_LEVEL"] = str(os.cpu_count())
    if not os.path.exists(self.build_temp):
      os.makedirs(self.build_temp)
    subprocess.check_call(
        ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp,
        env=env)

    # Build only pyspiel (for pip package)
    build_cmd = [
        "cmake", "--build", ".", "--target", "pyspiel", "--config", cfg
    ]
    subprocess.check_call(build_cmd, cwd=self.build_temp, env=env)

  def _select_cxx(self):
    if os.environ.get("CXX"):
      return os.environ.get("CXX")
    if hasattr(self, "_resolved_cxx"):
      return self._resolved_cxx
    return self._resolve_default_compiler()

  def _resolve_default_compiler(self):
    if sys.platform.startswith("win"):
      for candidate in ("clang-cl", "cl"):
        if shutil.which(candidate):
          return candidate
      return None
    return "clang++"

  def _check_compiler_version(self, cxx):
    if self._is_msvc(cxx):
      subprocess.check_call([cxx, "/?"])
    else:
      subprocess.check_call([cxx, "--version"])

  def _is_msvc(self, cxx):
    compiler = os.path.basename(cxx).lower()
    return compiler in ("cl", "cl.exe")


def _get_requirements(requirements_file):  # pylint: disable=g-doc-args
  """Returns a list of dependencies for setup() from requirements.txt.

  Currently a requirements.txt is being used to specify dependencies. In order
  to avoid specifying it in two places, we're going to use that file as the
  source of truth.
  """
  with open(requirements_file) as f:
    return [_parse_line(line) for line in f if line]


def _parse_line(s):
  """Parses a line of a requirements.txt file."""
  requirement, *_ = s.split("#")
  return requirement.strip()


# Get the requirements from file.
# When installing from pip it is in the parent directory
req_file = ""
if os.path.exists("requirements.txt"):
  req_file = "requirements.txt"
else:
  req_file = "../requirements.txt"

setuptools.setup(
    name="open_spiel",
    version="1.6.9",
    license="Apache 2.0",
    author="The OpenSpiel authors",
    author_email="open_spiel@google.com",
    description="A Framework for Reinforcement Learning in Games",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    url="https://github.com/deepmind/open_spiel",
    install_requires=_get_requirements(req_file),
    python_requires=">=3.10",
    ext_modules=[CMakeExtension("pyspiel", sourcedir="open_spiel")],
    cmdclass={"build_ext": BuildExt},
    zip_safe=False,
    packages=setuptools.find_packages(include=["open_spiel", "open_spiel.*"]),
)
