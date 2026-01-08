cmake_minimum_required(VERSION 3.20)
project(FinCompiler VERSION 0.1.0 LANGUAGES CXX C)

# set(CMAKE_CXX_STANDARD 20) -> conan handles this
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM targets: ${LLVM_AVAILABLE_LIBS}")
message(STATUS "LLVM INCLUDE DIR: ${LLVM_INCLUDE_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})

# NO NEED FOR SYSTEM-INSTALLED LLVM
# add_definitions(${LLVM_DEFINITIONS})
# llvm_map_components_to_libnames(llvm_libs core native support bitwriter irreader)
# llvm_map_components_to_libnames(LLVM_LIBS  Core  Support  IRReader)


# --- Generate Sources ---
BISON_TARGET(Parser src/parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
    COMPILE_FLAGS "-d -Wcounterexamples"
)
FLEX_TARGET(Lexer src/lexer/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# add_dependencies(FLEX_Lexer_OUTPUTS BISON_Parser_OUTPUTS)

# --- Core Library (The Compiler Engine) ---
add_library(fin_core STATIC
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
    
    # Diagnostics & Driver
    src/diagnostics/DiagnosticEngine.cpp
    src/driver/Driver.cpp 
    src/utils/ModuleLoader.cpp 
    
    # AST Base & Utilities
    src/ast/ASTNode.cpp
    src/ast/ASTPrinter.cpp
    src/ast/CloneVisitor.cpp
    
    # AST Nodes: Expressions
    src/ast/exprs/ArrayExpr.cpp
    src/ast/exprs/BinaryOp.cpp
    src/ast/exprs/FunctionCall.cpp
    src/ast/exprs/Identifier.cpp
    src/ast/exprs/Lambda.cpp
    src/ast/exprs/Literal.cpp
    src/ast/exprs/MiscExpr.cpp
    src/ast/exprs/PrototypeExpr.cpp
    src/ast/exprs/StructureExpr.cpp
    src/ast/exprs/UnaryOp.cpp
    
    # AST Nodes: Declarations
    src/ast/decls/ClassDecl.cpp
    src/ast/decls/DefineDecl.cpp
    src/ast/decls/FunctionDecl.cpp
    src/ast/decls/ImplementsBlock.cpp
    src/ast/decls/MacroDecl.cpp
    src/ast/decls/Program.cpp
    src/ast/decls/StructDecl.cpp
    src/ast/decls/TypeDef.cpp
    
    # AST Nodes: Statements
    src/ast/stmts/ControlFlow.cpp
    src/ast/stmts/ErrorHandling.cpp
    src/ast/stmts/Import.cpp
    src/ast/stmts/Statement.cpp
    src/ast/stmts/VariableDecl.cpp
    
    # AST Nodes: Types & Other
    src/ast/types/Attribute.cpp
    src/ast/types/GenericParam.cpp
    src/ast/types/TypeNode.cpp
    src/ast/nodes/Parameter.cpp
    
    # AST Cloning Implementations
    src/ast/cloning/CloneDecls.cpp
    src/ast/cloning/CloneExprs.cpp
    src/ast/cloning/CloneStmts.cpp
    src/ast/cloning/CloneTypes.cpp
    
    # Semantic Analysis
    src/semantics/impl/Analyzer_Core.cpp
    src/semantics/impl/Analyzer_Decl.cpp
    src/semantics/impl/Analyzer_Stmt.cpp
    src/semantics/impl/Analyzer_Expr.cpp
    
    # Types System
    src/types/Type.cpp
    src/types/ArrayType.cpp
    src/types/FunctionType.cpp
    src/types/GenericType.cpp
    src/types/PointerType.cpp
    src/types/PrimitiveType.cpp
    src/types/SelfType.cpp
    src/types/StructType.cpp
    
    # Macros & Preprocessor
    src/preprocessor/Preprocessor.cpp
    src/macros/MacroExpander.cpp
    src/macros/SubstitutionVisitor.cpp
    src/macros/expander/ExpanderDecls.cpp
    src/macros/expander/ExpanderExprs.cpp
    src/macros/expander/ExpanderStmts.cpp
    src/macros/expander/ExpanderTypes.cpp
)


target_link_libraries(fin_core PUBLIC 
    fmt::fmt 
    LLVM
    ${llvm_libs}
)

target_include_directories(fin_core PUBLIC 
    src 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LLVM_INCLUDE_DIRS}
)

# --- CLI Executable ---
add_executable(finc src/main.cpp)

target_link_libraries(finc PRIVATE fin_core)

# --- Testing ---
enable_testing()
add_subdirectory(tests)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
