# ParTeCL-CodeGen

A code generation tool, based on the Clang compiler, which generates OpenCL kernels for C programs. 
The purpose is to run their tests in parallel on the GPU threads.

The generated OpenCL code needs to be built and executed by ParTeCL-Runtime (see https://github.com/wyaneva/ParTeCL-Runtime).


I. Folder structure:
--------------------
  - **src**        Source code
  - **example**    A simple C program, together with a ParTeCL-CodeGen config file and test cases.

II. How to build:
-----------------
ParTeCL-CodeGen is a tool based on the Clang compiler, which uses Clang 4.0 LibTooling. 
As such, it lives in the clang-llvm source tree and is built as part of it.
Follow the following steps:
  0. Make sure you have CMake and Ninja installed.
  1. Then, download and build the Clang compiler, as described on http://clang.llvm.org/docs/LibASTMatchersTutorial.html.
  2. Next, clone ParTeCL-CodeGen inside ~/clang-llvm/llvm/tools/clang/tools/extra.
  3. Add the following line to ~/clang-llvm/llvm/tools/clang/tools/extra/CMakeLists.txt:
   
>      add_subdirectory(partecl-codegen)

  4. Create file ~/clang-llvm/llvm/tools/clang/tools/extra/partecl-codegen/CMakeLists.txt and add the following:

>      set(LLVM_LINK_COMPONENTS
>        Support
>        )
>              
>      add_clang_executable(partecl-codegen
>        src/ConfigParser.cpp
>        src/ConfigParser.h
>        src/Constants.h
>        src/CpuCodeGenerator.cpp
>        src/CpuCodeGenerator.h
>        src/Main.cpp
>        src/KernelGenerator.cpp
>        src/KernelGenerator.h
>        src/Utils.cpp
>        src/Utils.h)
>              
>      target_link_libraries(partecl-codegen
>        clangAST
>        clangASTMatchers
>        clangBasic
>        clangFrontend
>        clangTooling)

  5. You can now build ParTeCL-CodeGen:

>        cd ~/clang-llvm/build
>        ninja


III. How to run:
----------------

>  ~/clang-llvm/build/bin/partecl-codegen [source filenames] -config [config filename] -output [output directory] --

Compulsory arguments:
  - **[source filenames]**    names of the source files of the tested C program
  - **[config filename]**     name of the configuration file
  - **[output directory]**    path to the output directory, where the generated files should be stored

Example:

>  ~/clang-llvm/build/bin/partecl-codegen add.c -config add.config -output ~/partecl-runtime/kernel-gen
