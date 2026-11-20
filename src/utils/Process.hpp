// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fin {

// Arguments are passed literally. Empty redirection paths inherit the stream;
// equal output paths merge stdout/stderr. Environment overrides affect only the
// child; "\x01unset" removes a variable. Returns -1 when launch/wait fails.
int runProcess(const std::vector<std::string>& args,
               const std::string& out = {}, const std::string& err = {},
               const std::vector<std::pair<std::string, std::string>>& env = {});

// FIN_CC names a C driver, not a shell command. Preserve the exact output path
// on Windows too (clang otherwise appends .exe to an extensionless name).
std::vector<std::string> linkCommand(const std::vector<std::string>& objects,
                                     const std::string& output);

// First Homebrew llvm keg lib dir holding libLLVM.dylib, or "" on other
// hosts or when no keg is installed. The macOS default link line points
// -L at it; the test suite pins the line per host through this helper.
std::string brewLlvmLibDir();

}
