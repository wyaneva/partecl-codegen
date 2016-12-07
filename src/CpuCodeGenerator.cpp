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

#include <sstream>
#include <string>
#include "Constants.h"
#include "CpuCodeGenerator.h"

std::string generatePrintInt(
    const std::string& name)
{
  std::stringstream ss;
  ss << "printf(\"TC %d: %d\\n\", curres." << structs_constants::TEST_CASE_NUM << ", curres." << name << ");\n";
  return ss.str();
}

std::string generatePrintInts(
    const std::string& name,
    const int& size)
{
  std::stringstream ss;
  ss << "printf(\"TC %d: \", curres." << structs_constants::TEST_CASE_NUM << ");\n";
  ss << "for(int k = 0; k < " << size << "; k++)\n";
  ss << "{\n";
  ss << "  int curel = " << "curres." << name << "[k];\n";
  ss << "  printf(\"%d \", curel);\n";
  ss << "}\n";
  ss << "printf(\"\\n\");\n";
  return ss.str();
}

std::string generatePrintCalls(
    const struct ResultDeclaration& resultDecl)
{
  std::string str;
  if(resultDecl.declaration.type == "int")
  {
    if(resultDecl.declaration.isArray)
    {
      str = generatePrintInts(resultDecl.declaration.name, resultDecl.declaration.size);
    }
    else
    {
      str = generatePrintInt(resultDecl.declaration.name);
    }
  }
  else
  {
    //TODO: Handle other types
    llvm::outs() << "\ngenerateCompareResults: I don't know how to print results of type " << resultDecl.declaration.type << "\n";
  }

  return str;
}

void generateCompareResults(
    std::ofstream& strFile,
    const std::list<struct ResultDeclaration>& resultDecls)
{
  strFile << "void compare_results(struct " << structs_constants::RESULT << "* results, struct " << structs_constants::RESULT << "* exp_results, int num_test_cases)\n";
  strFile << "{\n";
  strFile << "  for(int i = 0; i < num_test_cases; i++)\n";
  strFile << "  {\n";
  strFile << "    struct " << structs_constants::RESULT << " curres = results[i];\n";
  for(auto& resultDecl : resultDecls)
  {
    //TODO: Find out how to compare; for now just print
    strFile << "    " << generatePrintCalls(resultDecl);
  }
  strFile << "  }\n";
  strFile << "}\n";
}

void generatePopulateInputs(
    std::ofstream& strFile,
    const std::list<struct Declaration>& inputDecls,
    const std::list<std::string>& stdinInputs)
{
  strFile << "void populate_inputs(struct input *input, int argc, char** args, int stdinc, char** stdins)\n";
  strFile << "{\n";

  strFile << "  (*input)." << structs_constants::TEST_CASE_NUM << " = atoi(args[0]);\n";
  strFile << "  (*input)." << structs_constants::ARGC << " = argc;\n";

  int i = -1;
  for(auto& input: inputDecls)
  {
    i++;

    std::string name = input.name;
    std::string argsidx = std::to_string(i+1);

    //for arrays, add a loop and indexes
    if(input.isArray)
    {
      strFile << "  for(int i = 0; i < " << input.size << "; i++)\n";
      name.append("[i]");
      argsidx = "i+";
      argsidx.append(std::to_string(i+1));
    }
    else
    {
      strFile << "  if(argc >= " << i+2 << ")\n";
    }

    //value
    //TODO: Add handling for other types
    if(input.type == "int")
    {
      strFile << "    (*input)." << name << " = " << "atoi(args[" << argsidx << "]);\n";
    }
    else if(input.type == "bool")
    {
      strFile << "    (*input)." << name << " = " << "atoi(args[" << argsidx << "]);\n";
    }
    else if(input.type == "char *" || input.type == "char*")
    {
      strFile << "    strcpy((*input)." << name << ", args[" << argsidx << "]);\n";
    }
    else
    {
      strFile << "args[" << argsidx << "];\n";
    }
  }

  for(auto& stdinArg: stdinInputs)
  {
    strFile << "  if(stdinc >= " << i+1 << ")\n";
    strFile << "    strcpy((*input)." << stdinArg << ", stdins[" << i << "]);\n";
  }

  strFile << "}\n";


}
