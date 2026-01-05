#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <set>

namespace fin {
    class Scope;
    class Program;
}

namespace fin {

class ModuleLoader {
public:
    ModuleLoader(const std::string& basePath);

    void addSearchPath(const std::string& path);

    std::shared_ptr<Scope> loadModule(const std::string& importPath, bool isPackage);

private:
    std::string rootBasePath; // Directory of the current file
    std::vector<std::string> searchPaths; // Global search paths (FIN_LIBS, -I)
    
    std::unordered_map<std::string, std::shared_ptr<Scope>> moduleCache;
    std::set<std::string> loadingStack;
    std::vector<std::unique_ptr<Program>> astStorage;

    std::string resolvePath(const std::string& importPath, bool isPackage);
    std::string readFile(const std::string& path);
};

}