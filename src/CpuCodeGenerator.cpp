/*
 * Copyright 2017 Vanya Yaneva, The University of Edinburgh
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
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

bool string_is_numeric(const std::string &str) {
  return find_if(str.begin(), str.end(), isdigit) == str.end();
}

bool is_valid_char(char c) { return std::isupper(c) || c == '_' || c == '-'; }

bool string_is_capital(const std::string &str) {
  return find_if(str.begin(), str.end(), is_valid_char) == str.end();
}

/*
 * Gnerate structs.h
 */
void generateDeclaration(std::ofstream &strFile,
                         const struct Declaration &declaration) {

  std::string type = declaration.type;
  std::stringstream size;
  // if the size is not a numeric string or compiler DEFINE, then hardcode it
  if (!string_is_numeric(declaration.size) &&
      !string_is_capital(declaration.size)) {
    size << structs_constants::POINTER_ARRAY_SIZE;
  } else {
    size << declaration.size;
  }

  if (declaration.isPointer) { // add the star to delcaraiont
    strFile << "  " << type << "* " << declaration.name << ";\n";
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
    const std::list<struct OutputDeclaration> &outputDeclarations,
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
  if (includes.size() > 0) {
    strFile << "\n";
  }
  strFile << "typedef struct " << structs_constants::INPUT << "{\n";
  strFile << "  int " << structs_constants::TEST_ID << ";\n";
  strFile << "  int " << structs_constants::ARGC << ";\n";

  for (auto &inputDecl : inputDeclarations) {
    generateDeclaration(strFile, inputDecl);
  }

  for (auto &stdinArg : stdinInputs) {
    generateDeclaration(strFile, stdinArg);
  }

  strFile << "} " << structs_constants::INPUT << ";\n\n";

  // write output
  strFile << "typedef struct " << structs_constants::OUTPUT << "{\n";
  strFile << "  int " << structs_constants::TEST_ID << ";\n";
  for (auto &outputDecl : outputDeclarations) {
    generateDeclaration(strFile, outputDecl.declaration);
  }
  strFile << "} " << structs_constants::OUTPUT << ";\n\n";

  strFile << "#endif\n";
  strFile.close();

  llvm::outs() << "DONE!\n";
}

/*
 * Generate cpu-gen.h and cpu-gen.c
 */

std::string generatePrintByTypeNonArray(const struct Declaration &declaration) {
  std::stringstream ss;

  ss << "printf(\"TC %d: \", curout." << structs_constants::TEST_ID << ");\n";

  if (declaration.type == "int") {

    ss << "printf(\"%d \\n\", curout." << declaration.name << ");\n";

  } else if (declaration.type == "char") {

    ss << "printf(\"%c \\n\", curout." << declaration.name << ");\n";

  } else {

    // TODO: Handle other types; currently default to int
#if ENABLE_WARNINGS
    llvm::outs() << "\ngenerateCompareOutputs: I don't know how to print "
                    "outputs of type '"
                 << outputDecl.declaration.type << "'. Defaulting to 'int'.\n";
#endif
    ss << "printf(\"%d \", curout." << declaration.name << ");\n";
  }
  return ss.str();
}

std::string generatePrintByTypeArray(const struct Declaration &declaration) {
  std::stringstream ss;
  if (declaration.type == "int") {

    ss << "      int curel = "
       << "curout." << declaration.name << "[k];\n";
    ss << "      printf(\"%d \", curel);\n";

  } else if (declaration.type == "char") {

    ss << "      char curel = "
       << "curout." << declaration.name << "[k];\n";
    ss << "      printf(\"%c \", curel);\n";

  } else {
    // TODO: Handle other types; currently default to int
#if ENABLE_WARNINGS
    llvm::outs() << "\ngenerateCompareOutputs: I don't know how to print "
                    "outputs of type '"
                 << declaration.type << "'. Defaulting to 'int'.\n";
#endif

    ss << "      int curel = "
       << "curout." << declaration.name << "[k];\n";
    ss << "      printf(\"%d \", curel);\n";
  }
  return ss.str();
}

std::string generatePrintArray(const struct Declaration &declaration) {

  std::string size = declaration.size;

  // if the size is not a numeric string, then it must be a member of the output
  // struct
  if (!string_is_numeric(declaration.size) &&
      !string_is_capital(declaration.size)) {
    size = "curout." + declaration.size;
  }

  std::stringstream ss;
  ss << "    printf(\"TC %d: \", curout." << structs_constants::TEST_ID
     << ");\n";
  ss << "    for(int k = 0; k < " << size << "; k++)\n";
  ss << "    {\n";
  ss << generatePrintByTypeArray(declaration);
  ss << "    }\n";
  ss << "    printf(\"\\n\");\n";
  return ss.str();
}

std::string generatePrintCalls(const struct OutputDeclaration &outputDecl) {
  std::string str;
  if (outputDecl.declaration.isArray) {
    str = generatePrintArray(outputDecl.declaration);
  } else {
    str = generatePrintByTypeNonArray(outputDecl.declaration);
  }

  return str;
}

void generateCompareOutputs(
    std::ofstream &strFile,
    const std::list<struct OutputDeclaration> &outputDecls) {
  strFile << "void compare_outputs(struct " << structs_constants::OUTPUT
          << "* outputs, struct " << structs_constants::OUTPUT
          << "* exp_outputs, int num_tests)\n";
  strFile << "{\n";
  strFile << "  for(int i = 0; i < num_tests; i++)\n";
  strFile << "  {\n";
  strFile << "    struct " << structs_constants::OUTPUT
          << " curout = outputs[i];\n";
  for (auto &outputDecl : outputDecls) {
    // TODO: Find out how to compare; for now just print
    strFile << generatePrintCalls(outputDecl);
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
    std::stringstream size;
    // if the size is not a numeric string or DEFINE constant, then make it a
    // variable name
    if (!string_is_numeric(input.size) && !string_is_capital(input.size)) {
      size << "input->" << input.size;
    } else {
      size << input.size;
    }

    strFile << "  for(int i = 0; i < " << size.str() << "; i++)\n";
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
    strFile << "    int len = strlen(" << container << "[" << argsidx << "]);\n"
            << "    input->" << name
            << " = (char *)malloc(sizeof(char)*(len + 1));\n"
            << "    strcpy(input->" << name << ", " << container << "["
            << argsidx << "]);\n";
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
          << " *input, int num_values, char** values)\n";
  strFile << "{\n";

  strFile << "  input->" << structs_constants::TEST_ID
          << " = atoi(values[0]);\n";

  strFile << "  input->" << structs_constants::ARGC << " = "
          << inputDecls.size() + 1 << ";\n";

  int i = -1; // command line args start from index 1
  for (auto &input : inputDecls) {
    i++;
    generatePopulateInput(strFile, input, "num_values", "values", i);
  }
  for (auto &stdinArg : stdinInputs) {
    i++;
    generatePopulateInput(strFile, stdinArg, "num_values", "values", i);
  }

  strFile << "}\n";
}

void generateCpuGen(const std::string &outputDirectory,
                    const std::list<struct Declaration> &inputs,
                    const std::list<struct OutputDeclaration> &outputs,
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
             << "*, int, char**);\n\n";
  headerFile << "void compare_outputs(struct " << structs_constants::OUTPUT
             << "*, struct " << structs_constants::OUTPUT << "*, int);\n\n";
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
  generateCompareOutputs(strFile, outputs);

  strFile.close();

  llvm::outs() << "DONE!\n";
}
