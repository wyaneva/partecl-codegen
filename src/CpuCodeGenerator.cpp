/*
 * Copyright 2017-2018 Vanya Yaneva, The University of Edinburgh
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

#include "CpuCodeGenerator.h"
#include "Constants.h"
#include "Utils.h"
#include <sstream>
#include <string>

/*
 * Gnerate structs.h
 */
void generateDeclaration(std::ofstream &strFile,
                         const struct Declaration &declaration) {

  std::string type = declaration.type;
  std::stringstream size;
  // if the size is not a numeric string, then hardcode it
  if (declaration.size.find_first_not_of("0123456789") != std::string::npos)
    size << structs_constants::PADDED_INPUT_ARRAY_SIZE;
  else
    size << declaration.size;

  if (declaration.isPointer) { // turn a pointer into a static array
    strFile << "  " << type << " " << declaration.name << "["
            << structs_constants::PADDED_INPUT_ARRAY_SIZE << "];\n";
  } else if (declaration.isArray) {
    strFile << "  " << type << " " << declaration.name << "[" << size.str()
            << "];\n";
  } else {
    strFile << "  " << type << " " << declaration.name << ";\n";
  }
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
  strFile << "#define " << structs_constants::PADDED_INPUT_ARRAY_SIZE << " "
          << structs_constants::POINTER_ARRAY_SIZE << "\n";
  strFile << "\n";
  strFile << "typedef struct " << structs_constants::INPUT << "\n";
  strFile << "{\n";
  strFile << "  int " << structs_constants::TEST_CASE_NUM << ";\n";
  strFile << "  int " << structs_constants::ARGC << ";\n";

  for (auto &inputDecl : inputDeclarations) {
    generateDeclaration(strFile, inputDecl);
  }

  for (auto &stdinArg : stdinInputs) {
    generateDeclaration(strFile, stdinArg);
  }

  strFile << "} " << structs_constants::INPUT << ";\n\n";

  // write result
  strFile << "typedef struct " << structs_constants::RESULT << "\n";
  strFile << "{\n";
  strFile << "  int test_case_num;\n";
  for (auto &resultDecl : resultDeclarations) {
    generateDeclaration(strFile, resultDecl.declaration);
  }
  strFile << "} " << structs_constants::RESULT << ";\n\n";

  strFile << "#endif\n";
  strFile.close();

  llvm::outs() << "DONE!\n";
}

/*
 * Generate cpu-gen.h and cpu-gen.c
 */

std::string generatePrintByTypeNonArray(const struct Declaration &declaration) {
  std::stringstream ss;

  ss << "printf(\"TC %d: \", curres." << structs_constants::TEST_CASE_NUM
     << ");\n";

  if (declaration.type == "int") {

    ss << "printf(\"%d \\n\", curres." << declaration.name << ");\n";

  } else if (declaration.type == "char") {

    ss << "printf(\"%c \\n\", curres." << declaration.name << ");\n";

  } else {

    // TODO: Handle other types; currently default to int
#if ENABLE_WARNINGS
    llvm::outs() << "\ngenerateCompareResults: I don't know how to print "
                    "results of type '"
                 << resultDecl.declaration.type << "'. Defaulting to 'int'.\n";
#endif
    ss << "printf(\"%d \", curres." << declaration.name << ");\n";
  }
  return ss.str();
}

std::string generatePrintByTypeArray(const struct Declaration &declaration) {
  std::stringstream ss;
  if (declaration.type == "int") {

    ss << "      int curel = "
       << "curres." << declaration.name << "[k];\n";
    ss << "      printf(\"%d \", curel);\n";

  } else if (declaration.type == "char") {

    ss << "      char curel = "
       << "curres." << declaration.name << "[k];\n";
    ss << "      printf(\"%c \", curel);\n";

  } else {
    // TODO: Handle other types; currently default to int
#if ENABLE_WARNINGS
    llvm::outs() << "\ngenerateCompareResults: I don't know how to print "
                    "results of type '"
                 << declaration.type << "'. Defaulting to 'int'.\n";
#endif

    ss << "      int curel = "
       << "curres." << declaration.name << "[k];\n";
    ss << "      printf(\"%d \", curel);\n";
  }
  return ss.str();
}

std::string generatePrintArray(const struct Declaration &declaration) {

  std::string size = declaration.size;

  // if the size is not a numeric string, then it must be a member of the result
  // struct
  if (declaration.size.find_first_not_of("0123456789") != std::string::npos)
    size = "curres." + declaration.size;

  std::stringstream ss;
  ss << "    printf(\"TC %d: \", curres." << structs_constants::TEST_CASE_NUM
     << ");\n";
  ss << "    for(int k = 0; k < " << size << "; k++)\n";
  ss << "    {\n";
  ss << generatePrintByTypeArray(declaration);
  ss << "    }\n";
  ss << "    printf(\"\\n\");\n";
  return ss.str();
}

std::string generatePrintCalls(const struct ResultDeclaration &resultDecl) {
  std::string str;
  if (resultDecl.declaration.isArray) {
    str = generatePrintArray(resultDecl.declaration);
  } else {
    str = generatePrintByTypeNonArray(resultDecl.declaration);
  }

  return str;
}

void generateCompareResults(
    std::ofstream &strFile,
    const std::list<struct ResultDeclaration> &resultDecls) {
  strFile << "void compare_results(struct " << structs_constants::RESULT
          << "* results, struct " << structs_constants::RESULT
          << "* exp_results, int num_test_cases)\n";
  strFile << "{\n";
  strFile << "  for(int i = 0; i < num_test_cases; i++)\n";
  strFile << "  {\n";
  strFile << "    struct " << structs_constants::RESULT
          << " curres = results[i];\n";
  for (auto &resultDecl : resultDecls) {
    // TODO: Find out how to compare; for now just print
    strFile << generatePrintCalls(resultDecl);
  }
  strFile << "  }\n";
  strFile << "}\n";
}

void generatePopulateInput(std::ofstream &strFile, struct Declaration input,
                           std::string count, std::string container, int i) {
  std::string name = input.name;
  std::string argsidx = std::to_string(i + 1);

  // for arrays, add a loop and indexes
  if (input.isArray) {
    strFile << "  for(int i = 0; i < " << input.size << "; i++)\n";
    name.append("[i]");
    argsidx = "i+";
    argsidx.append(std::to_string(i + 1));
  } else {
    strFile << "  if(" << count << " >= " << i + 2 << ")\n"
            << "  {\n";
  }

  // pointers
  // TODO: add handling for pointers which are not 'char *'
  if (input.isPointer) {
    strFile << "    char *" << name << "_ptr = " << container << "[" << argsidx
            << "];\n"
            << "    int idx = 0;\n"
            << "    while(*" << name << "_ptr != \'\\0\')\n"
            << "    {\n"
            << "      input->" << name << "[idx] = *" << name << "_ptr;\n"
            << "      " << name << "_ptr++;\n"
            << "      idx++;\n"
            << "    }\n"
            << "    for(int i = idx; i < "
            << structs_constants::PADDED_INPUT_ARRAY_SIZE << "; i++)\n"
            << "    {\n"
            << "    input->" << name << "[i] = \'\\0\';\n"
            << "    }\n";
    // not pointers
  } else if (contains(input.type, "int")) {
    strFile << "    input->" << name << " = "
            << "atoi(" << container << "[" << argsidx << "]);\n";
  } else if (contains(input.type, "bool")) {
    strFile << "    input->" << name << " = "
            << "atoi(" << container << "[" << argsidx << "]);\n";
  } else if (contains(input.type, "char")) {
    strFile << "    input->" << name << " = "
            << "*" << container << "[" << argsidx << "];\n";
  } else {
#if ENABLE_WARNINGS
    llvm::outs() << "POPULATE_INPUTS: Improvising for custom type "
                 << input.type << ".\n";
#endif
    strFile << "    input->" << name << " = "
            << "atoi(" << container << "[" << argsidx << "]);\n";
  }

  // close bracket opened in the beginning
  if (!input.isArray) {
    strFile << "  }\n";
  }
}

void generatePopulateInputs(std::ofstream &strFile,
                            const std::list<struct Declaration> &inputDecls,
                            const std::list<struct Declaration> &stdinInputs) {
  strFile << "void populate_inputs(struct " << structs_constants::INPUT
          << " *input, int argc, char** args, int stdinc, char** stdins)\n";
  strFile << "{\n";

  strFile << "  input->" << structs_constants::TEST_CASE_NUM
          << " = atoi(args[0]);\n";
  strFile << "  input->" << structs_constants::ARGC << " = argc;\n";

  int i = -1; // command line args start from index 1
  for (auto &input : inputDecls) {
    i++;

    generatePopulateInput(strFile, input, "argc", "args", i);
  }

  i = -2; // stdin args start from index 0
  for (auto &stdinArg : stdinInputs) {
    i++;
    generatePopulateInput(strFile, stdinArg, "stdinc", "stdins", i);
  }

  strFile << "}\n";
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
