# ParTeCL-CodeGen

A code generation tool, based on the Clang compiler, which generates OpenCL kernels for C programs. 
The purpose is to run their tests in parallel on the GPU threads.

The generated OpenCL code needs to be built and executed by [ParTeCL-Runtime](https://github.com/wyaneva/ParTeCL-Runtime).


## Folder structure

  - **src:**        Source code
  - **example:**    Simple C programs, together with a configuration file and test cases
  - **doc:**        More detailed documentation


## How to build

**ParTeCL-CodeGen** is a tool based on the Clang compiler, which uses Clang 4.0 LibTooling. 
As such, it lives in the **clang-llvm** source tree and is built as part of it.
Follow the following steps:

0. Make sure you have [CMake](https://cmake.org) and [Ninja](https://ninja-build.org) installed.
1. Download and build the Clang compiler, as described [here](http://clang.llvm.org/docs/LibASTMatchersTutorial.html).
2. Clone **ParTeCL-CodeGen** inside `$HOME/clang-llvm/llvm/tools/clang/tools/extra`.
3. Add the following line to `$HOME/clang-llvm/llvm/tools/clang/tools/extra/CMakeLists.txt`:
```
add_subdirectory(partecl-codegen)
```

4. Build **ParTeCL-CodeGen**:

```
cd $HOME/clang-llvm/build
ninja
```

This will create an executable **partecl-codegen** inside `$HOME/clang-llvm/build/bin/`.
You can make it executable from anywhere on your system by creating a link to it in, eg. `/usr/local/bin`.


## How to run

  ```
  partecl-codegen [source filenames] -config [config filename] -output [output directory] --
  ```

Compulsory arguments:
  - **[source filenames]**    names of the source files of the tested C program
  - **[config filename]**     name of the configuration file
  - **[output directory]**    path to the output directory, where the generated files should be stored

Example:

  ```  
  partecl-codegen add.c -config add.config -output ~/partecl-runtime/kernel-gen
  ```


## Project figure

The following figure displays how **ParTeCL-CodeGen** is used with [ParTeCL-Runtime](https://github.com/wyaneva/ParTeCL-Runtime).

![ParTeCL figure](doc/images/partecl.svg)


## Related publications

1. [**Compiler-Assisted Test Acceleration on GPUs for Embedded Software**](https://dl.acm.org/citation.cfm?id=3092720),  
   *Vanya Yaneva, Ajitha Rajan, and Christophe Dubach*, In ISSTA'17, July 2017

2. [**ParTeCL - Parallel Testing Using OpenCL**](https://dl.acm.org/citation.cfm?id=3098227),  
   *Vanya Yaneva, Ajitha Rajan, and Christophe Dubach*, In ISSTA'17, July 2017

