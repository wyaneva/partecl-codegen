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

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "KernelGenerator.h"
#include "Utils.h"
#define CPU_GEN_FILENAME "cpu-gen"

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using namespace std;

static llvm::cl::OptionCategory MscToolCategory("kernel-gen options");
static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

//private function declarations
void generateStructs(string, string, map<int, string>&, list<string>&, struct TestedValue&, list<struct declaration>&, list<struct declaration>&); 
void generatePopulateInputs(string, list<struct declaration>, list<string>);

int main(int argc, const char **argv)
{
  if(argc <= 4)
  {
    cout << "Correct usage: \n ./partecl-codegen [source filename] -- [config filename] [output directory] \n Exiting execution without generating anything. \n";
    return 0;
  }

  struct TestedValue testedValue;
  map<int, string> argvIdxToInput;
  list<string> stdinInputs;
  list<struct declaration> inputDeclarations;
  list<struct declaration> resultDeclarations;

  //TODO: the testing params come after '--'
  string configFilename = argv[3];
  string outputDirectory = argv[4];
  generateStructs(configFilename, outputDirectory, argvIdxToInput, stdinInputs, testedValue, inputDeclarations, resultDeclarations);
  generatePopulateInputs(outputDirectory, inputDeclarations, stdinInputs);

  CommonOptionsParser OptionsParser(argc, argv, MscToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
  
  generateKernel(&Tool, outputDirectory, argvIdxToInput, stdinInputs, resultDeclarations, testedValue);
}


//private methods
void generateStructs(
    string configFilename, 
    string outputDirectory, 
    map<int, string>& argvIdxToInput, 
    list<string>& stdinInputs,
    struct TestedValue& testedValue,
    list<struct declaration>& inputDeclarations,
    list<struct declaration>& resultDeclarations)
{
  llvm::outs() << "Generating memory buffer structs... ";

  ifstream infile(configFilename);
  string line;

  int inputDeclsCount = 0;
  int resultDeclsCount = 0;
  while(getline(infile, line))
  {
    istringstream iss(line);
    string annot, type, name;
    int argvIdx;

    iss >> annot;

    //find out the name of the tested function
    if(annot == "function:")
    {
      testedValue.type = TestedValueType::functionCall;
      iss >> testedValue.name;

      string returnType;
      iss >> returnType;
      if(returnType == "RET")
      {
        testedValue.resultArg = -1;
      }
      else if(returnType == "ARG")
      {
        string argNumStr;
        iss >> argNumStr;
        int argNum = stoi(argNumStr);
        if(argNum < 1)
        {
          llvm::outs() << argNum << " is not a valid argument number config file line:\n" << line << "\nTerminating!\n";
          return;
        }

        testedValue.resultArg = argNum;
      }
      else
      {
        llvm::outs() << returnType << " is not a valid result in config file line:\n" << line << "\nPlease enter RET or ARG.\nTerminating!\n";
        return;
      }
    }

    //find the inputs
    if(annot == "input:")
    {
      iss >> type >> name >> argvIdx;

      struct declaration input;
      input.type = type;
      input.name = name;
      inputDeclarations.push_back(input);
      inputDeclsCount++;

      //add to maps
      argvIdxToInput[argvIdx] = name;
    }

    if(annot == "stdin:")
    {
      iss >> type >> name;

      //add to list of inputs
      stdinInputs.push_back(name);
    }

    //find the results
    if(annot == "result:")
    {
      iss  >> type >> name; 

      struct declaration decl;
      decl.name = name;
      decl.type = type;
      resultDeclarations.push_back(decl);
      resultDeclsCount++;
    }
  }

  //create the structs file
  string structsFilename = outputDirectory + '/' + STRUCTS_FILENAME;
  ofstream strFile;
  strFile.open(structsFilename);
 
  //write input
  strFile << "#ifndef STRUCTS_H\n";
  strFile << "#define STRUCTS_H\n\n";
  strFile << "typedef struct input\n";
  strFile << "{\n";
  strFile << "  int test_case_num;\n";
  strFile << "  int argc;\n";
  for(int i = 0; i < inputDeclsCount; i++)
  {
    struct declaration currentInput = inputDeclarations.front();
    inputDeclarations.pop_front();
    //TODO: Decide on an intelligent way to determine the size of the static decls
    string type = currentInput.type;
    auto starChar = type.find("*");
    if(starChar != string::npos)
    {
      type.erase(starChar);
      strFile << "  " << type << " " << currentInput.name << "[500];\n";
    }
    else
    {
      strFile << "  " << type << " " << currentInput.name << ";\n";
    }
    inputDeclarations.push_back(currentInput);
  }

  int numStdins = stdinInputs.size();
  for(int i = 0; i < numStdins; i++)
  {
    string stdinArg = stdinInputs.front();
    stdinInputs.pop_front();
    strFile << "  " << "char " << " " << stdinArg << "[500];\n";
    stdinInputs.push_back(stdinArg);
  }
  strFile << "} input;\n\n";

  //write result
  strFile << "typedef struct result\n";
  strFile << "{\n";
  strFile << "  int test_case_num;\n";
  for(int i = 0; i < resultDeclsCount; i++)
  {
    auto resultDecl = resultDeclarations.front();
    resultDeclarations.pop_front();
    string type = resultDecl.type;
    auto starChar = type.find("*");
    if(starChar != string::npos)
    {
      type.erase(starChar);
      strFile << "  " << type << " " << resultDecl.name <<"[500];\n";
    }
    else
    {
      strFile << "  " << type << " " << resultDecl.name <<";\n";
    }

    resultDeclarations.push_back(resultDecl);
  }
  strFile << "} result;\n\n";

  strFile << "#endif\n";
  strFile.close();

  llvm::outs() << "DONE!\n";
}

void generatePopulateInputs(string outputDirectory, list<struct declaration> inputs, list<string> stdinInputs)
{
  llvm::outs() << "Generating CPU code... ";

  //generate header file
  ofstream headerFile;
  string headerFilename = outputDirectory + "/" + CPU_GEN_FILENAME + ".h";
  headerFile.open(headerFilename);

  headerFile << "#ifndef CPU_GEN_H\n";
  headerFile << "#define CPU_GEN_H\n";
  headerFile << "#include \"structs.h\"\n\n";
  headerFile << "void populate_inputs(struct input*, int, char**, int, char**);\n\n";
  headerFile << "#endif\n";

  headerFile.close();

  //generate source file
  ofstream strFile;
  string sourceFilename = outputDirectory + "/" + CPU_GEN_FILENAME + ".c";
  strFile.open(sourceFilename);

  strFile << "#include <stdlib.h>\n";
  strFile << "#include <string.h>\n";
  strFile << "#include \"cpu-gen.h\"\n\n";
  strFile << "void populate_inputs(struct input *input, int argc, char** args, int stdinc, char** stdins)\n";
  strFile << "{\n";

  strFile << "  (*input).test_case_num = atoi(args[0]);\n";
  strFile << "  (*input).argc = argc;\n";

  int numInputs = inputs.size();
  for(int i = 0; i < numInputs; i++)
  {
    struct declaration currentInput = inputs.front();
    inputs.pop_front();

    strFile << "  if(argc >= " << i+2 << ")\n";
    //value
    //TODO: Add handling for other types
    if(currentInput.type == "int")
    {
      strFile << "    (*input)." << currentInput.name << " = " << "atoi(args[" << i+1 << "]);\n";
    }
    else if(currentInput.type == "bool")
    {
      strFile << "    (*input)." << currentInput.name << " = " << "atoi(args[" << i+1 << "]);\n";
    }
    else if(currentInput.type == "char *" || currentInput.type == "char*")
    {
      strFile << "    strcpy((*input)." << currentInput.name << ", args[" << i+1 << "]);\n";
    }
    else
    {
      strFile << "args[" << i+1 << "];\n";
    }
  }

  int numStd = stdinInputs.size();
  for(int i = 0; i < numStd; i++)
  {
    string stdinArg = stdinInputs.front();
    stdinInputs.pop_front();
    strFile << "  if(stdinc >= " << i+1 << ")\n";
    strFile << "    strcpy((*input)." << stdinArg << ", stdins[" << i << "]);\n";
  }

  strFile << "}\n";

  strFile.close();

  llvm::outs() << "DONE!\n";
}
