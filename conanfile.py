# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️ for someone special.
#
# This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
# Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
# Fin is the name of the Angel from BlackNightmare-Stories not yet published.
# “Love is the reason this project exists”
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778/Fin
# Profile: https://github.com/M1778M/
# -----------------------------------------------------------------------------
# Copyright (C) 2027 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================

import os
import re

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import cmake_layout


class FinConan(ConanFile):
    """Dependency manifest for the Fin compiler (finc).

    ADR 0010 -- one LLVM major, proven by CI -- governs this file.  Note what is
    *not* here: llvm-core.  See `requirements()` for why, and for the command
    that reproduces the reason.
    """

    name = "fin"
    settings = "os", "arch", "compiler", "build_type"
    generators = ("CMakeDeps", "CMakeToolchain")

    def set_version(self):
        """Read the version out of CMakeLists.txt instead of repeating it.

        `project(FinCompiler VERSION ...)` is the one place the compiler's
        version is written down.  A second copy here would be a copy that can
        drift, and the copy that drifts is the one that names the release
        archives -- so it is parsed rather than duplicated.
        """
        cmakelists = os.path.join(self.recipe_folder, "CMakeLists.txt")
        with open(cmakelists, encoding="utf-8") as handle:
            text = handle.read()
        match = re.search(
            r"project\s*\(\s*FinCompiler\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", text)
        if not match:
            raise ConanException(
                "no `project(FinCompiler VERSION <x.y.z>)` in %s; that line is "
                "where the version lives" % cmakelists)
        self.version = match.group(1)

    def requirements(self):
        # fmt is used by the driver and the diagnostic engine on every platform.
        self.requires("fmt/10.2.1")

        # gtest builds `fin_tests` only, so it is a test_requires: it stays out
        # of the graph a release build resolves.
        self.test_requires("gtest/1.14.0")

        # --- LLVM (ADR 0010) -------------------------------------------------
        # The decision is a single LLVM major -- 18 -- on every platform.  It is
        # NOT satisfiable from ConanCenter, which publishes no LLVM 18 at all:
        #
        #     conan search llvm-core -r=conancenter
        #     -> 11.1.0, 12.0.0, 13.0.0, 19.1.7
        #
        # The Windows-only `llvm-core/19.1.7` branch this file used to carry has
        # been deleted rather than generalised, because generalising it would
        # pin every platform to 19 and ADR 0010 chose 18 deliberately ("18
        # rather than 19 because 18 is what the build is verified against").
        #
        # Nothing in src/ includes an LLVM header yet -- `runCodeGen` returns
        # true without emitting (plan, wave 5) -- so the pin buys nothing today
        # and costs the CI matrix three of its six platforms, since ConanCenter
        # has no llvm-core binary for Linux armv8, macOS x86_64 or Windows
        # armv8 at any version.  The version is pinned and *enforced* in
        # CMakeLists.txt (FIN_LLVM_MAJOR) so that the rule is checked the moment
        # LLVM is switched on; wave 5 has to choose where the binaries come
        # from.  This is flagged for the project owner, not settled here.

    def layout(self):
        cmake_layout(self)
