#include <iostream>
#include <vector>
#include <string>
#include "driver/Driver.hpp"
#include <fmt/core.h>

void printUsage() {
    fmt::print("Usage: finc <file.fin> [options]\n");
    fmt::print("Options:\n");
    fmt::print("  --debug-ast      Print the parsed AST\n");
    fmt::print("  --debug-sema     Print semantic analysis details\n");
    fmt::print("  --no-check       Skip semantic analysis (Unsafe)\n");
    fmt::print("  --help           Show this message\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    fin::CompilerOptions opts;
    std::vector<std::string> args(argv + 1, argv + argc);
    
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--help") { printUsage(); return 0; }
        else if (arg == "--debug-ast") opts.debugParser = true;
        else if (arg == "--debug-sema") opts.debugSema = true;
        else if (arg == "--no-check") opts.skipSemantics = true;
        else if (arg == "-I" || arg == "--include") {
            if (i + 1 < args.size()) {
                opts.includePaths.push_back(args[++i]);
            } else {
                fmt::print("Error: Missing path for -I\n");
                return 1;
            }
        }
        else if (arg[0] != '-') opts.inputFile = arg;
    }

    if (opts.inputFile.empty()) {
        fmt::print("Error: No input file specified.\n");
        return 1;
    }

    fin::Driver driver(opts);
    return driver.compile();
}