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

// private function declarations
void generateStructs(const std::string &, const std::list<struct Declaration> &,
                     const std::list<struct Declaration> &,
                     const std::list<struct ResultDeclaration> &,
                     const std::list<std::string> &);
void generateCpuGen(const std::string &, const std::list<struct Declaration> &,
                    const std::list<struct ResultDeclaration> &,
                    const std::list<struct Declaration> &);

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

void generateStructs(
    const std::string &outputDirectory,
    const std::list<struct Declaration> &stdinInputs,
    const std::list<struct Declaration> &inputDeclarations,
    const std::list<struct ResultDeclaration> &resultDeclarations,
    const std::list<std::string> &includes) {
  llvm::outs() << "Generating memory buffer structs... ";

  std::string structsFilename =
      outputDirectory + '/' + filename_constants::STRUCTS_FILENAME;
  std::ofstream strFile;
  strFile.open(structsFilename);

  // write input
  strFile << "#ifndef STRUCTS_H\n";
  strFile << "#define STRUCTS_H\n\n";
  for (auto &include : includes) {
    strFile << "#include \"" << include << "\"\n";
  }
  strFile << "\n";
  strFile << "typedef struct " << structs_constants::INPUT << "\n";
  strFile << "{\n";
  strFile << "  int " << structs_constants::TEST_CASE_NUM << ";\n";
  strFile << "  int " << structs_constants::ARGC << ";\n";

  for (auto &currentInput : inputDeclarations) {
    std::string type = currentInput.type;
    if (currentInput.isArray) {
      strFile << "  " << type << " " << currentInput.name << "["
              << currentInput.size << "];\n";
    } else {
      strFile << "  " << type << " " << currentInput.name << ";\n";
    }
  }

  for (auto &stdinArg : stdinInputs) {
    strFile << "  " << stdinArg.type << " " << stdinArg.name << ";\n";
  }

  strFile << "} " << structs_constants::INPUT << ";\n\n";

  // write result
  strFile << "typedef struct " << structs_constants::RESULT << "\n";
  strFile << "{\n";
  strFile << "  int test_case_num;\n";
  for (auto &resultDecl : resultDeclarations) {
    std::string type = resultDecl.declaration.type;
    auto starChar = type.find("*");
    if (starChar != std::string::npos) {
      type.erase(starChar);
      strFile << "  " << type << " " << resultDecl.declaration.name
              << "[500];\n";
    } else if (resultDecl.declaration.isArray) {
      strFile << "  " << type << " " << resultDecl.declaration.name << "["
              << resultDecl.declaration.size << "];\n";
    } else {
      strFile << "  " << type << " " << resultDecl.declaration.name << ";\n";
    }
  }
  strFile << "} " << structs_constants::RESULT << ";\n\n";

  strFile << "#endif\n";
  strFile.close();

  llvm::outs() << "DONE!\n";
}

void generateCpuGen(const std::string &outputDirectory,
                    const std::list<struct Declaration> &inputs,
                    const std::list<struct ResultDeclaration> &results,
                    const std::list<struct Declaration> &stdinInputs) {
  llvm::outs() << "Generating CPU code... ";

  // generate header file
  std::ofstream headerFile;
  std::string headerFilename =
      outputDirectory + "/" + filename_constants::CPU_GEN_FILENAME + ".h";
  headerFile.open(headerFilename);

  headerFile << "#ifndef CPU_GEN_H\n";
  headerFile << "#define CPU_GEN_H\n";
  headerFile << "#include \"structs.h\"\n\n";
  headerFile << "void populate_inputs(struct " << structs_constants::INPUT
             << "*, int, char**, int, char**);\n\n";
  headerFile << "void compare_results(struct " << structs_constants::RESULT
             << "*, struct " << structs_constants::RESULT << "*, int);\n\n";
  headerFile << "#endif\n";

  headerFile.close();

  // generate source file
  std::ofstream strFile;
  std::string sourceFilename =
      outputDirectory + "/" + filename_constants::CPU_GEN_FILENAME + ".c";
  strFile.open(sourceFilename);

  strFile << "#include <stdlib.h>\n";
  strFile << "#include <string.h>\n";
  strFile << "#include <stdio.h>\n";
  strFile << "#include \"cpu-gen.h\"\n\n";

  generatePopulateInputs(strFile, inputs, stdinInputs);
  generateCompareResults(strFile, results);

  strFile.close();

  llvm::outs() << "DONE!\n";
}
