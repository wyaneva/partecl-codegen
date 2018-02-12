/*
 * Copyright 2016 Vanya Yaneva, The University of Edinburgh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ConfigParser.h"
#include "Constants.h"
#include "CpuCodeGenerator.h"
#include "KernelGenerator.h"
#include "Utils.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static llvm::cl::OptionCategory MscToolCategory("kernel-gen options");
static llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

// ParTeCL-CodeGen requires the following command line options:
//  config filename
//  output directory
static llvm::cl::opt<std::string>
    ConfigFilename("config", llvm::cl::desc("Specify config filename"),
                   llvm::cl::value_desc("filename"), llvm::cl::Required);
static llvm::cl::opt<std::string>
    OutputDir("output", llvm::cl::desc("Specify output directory"),
              llvm::cl::value_desc("dir"), llvm::cl::Required);

int main(int argc, const char **argv) {
  clang::tooling::CommonOptionsParser OptionsParser(argc, argv,
                                                    MscToolCategory);

  std::map<int, std::string> argvIdxToInput;
  std::list<struct Declaration> stdinInputs;
  std::list<struct Declaration> inputDeclarations;
  std::list<struct ResultDeclaration> resultDeclarations;
  std::list<std::string> includes;

  // parse the configuration file
  if (parseConfig(ConfigFilename, argvIdxToInput, stdinInputs,
                  inputDeclarations, resultDeclarations,
                  includes) == status_constants::FAIL) {
    llvm::outs() << "\nFailed to parse the configuration file "
                 << ConfigFilename << ". \nTERMINATING!\n";
    return status_constants::FAIL;
  }

  // generate the struct file
  generateStructs(OutputDir, stdinInputs, inputDeclarations, resultDeclarations,
                  includes);
  // generate CPU code
  generateCpuGen(OutputDir, inputDeclarations, resultDeclarations, stdinInputs);

  // generate kernel
  clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                 OptionsParser.getSourcePathList());

  generateKernel(&Tool, OutputDir, argvIdxToInput, inputDeclarations,
                 stdinInputs, resultDeclarations);
}
