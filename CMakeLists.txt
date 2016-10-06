set(LLVM_LINK_COMPONENTS
  Support
  )
        
add_clang_executable(partecl-codegen
  src/Main.cpp
  src/KernelGenerator.cpp
  src/KernelGenerator.h
  src/Utils.cpp
  src/Utils.h)
        
target_link_libraries(partecl-codegen
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling)
