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
