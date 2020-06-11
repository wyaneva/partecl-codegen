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

#include "Constants.h"
#include "Utils.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

#define INPUT_1 1
#define OUTPUT_1 2

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;

LangOptions langOpts;
PrintingPolicy printingPolicy(langOpts);

// Global scope members
bool isMainFile = false;
std::string testClOutputDirectory;
std::map<int, std::string> argvIdxToInput;
std::map<const Expr *, bool> argvIdxToIsReplaced;
std::map<const SourceLocation, bool> locationToPointerDereferenced;
std::list<struct Declaration> inputs;
std::map<std::string, bool> inputsToIsAddedDeclaration;
std::list<struct Declaration> stdinInputs;
std::list<struct OutputDeclaration> outputs;

// a list of all the global vars
std::list<const VarDecl *> globalVars;

// a map of all global vars used in a function declaration (they need to be
// added to the parameter list) //both original uses and added as parameters to
// function calls
std::map<const FunctionDecl *, std::vector<const ValueDecl *>> funcToGlobalVars;
std::vector<const FunctionDecl *> functionsWhichUseTestInputs;
std::vector<const FunctionDecl *> functionsWhichUseTestOutputs;
std::vector<const FunctionDecl *> functionsWhichUseStdin;

// a map of function calls to their caller
std::map<const CallExpr *, const FunctionDecl *> funcCallToCallerDecl;

// a map of function declarations to all their callers
std::map<const FunctionDecl *, std::list<const FunctionDecl *>>
    funcDeclToCallerDecls;

// a map which contains all parameters that were added to a function
// declaration, together with commas in front of them
std::map<const FunctionDecl *, std::list<std::string>> funcToAddedParameters;

// a map which contains all arguments that were added to a function call,
// together with commas in front of them
std::map<const CallExpr *, std::list<std::string>> funcCallToAddedArgs;

// a list of include files to add
std::list<std::string> includesToAdd;

void replaceSourceRange(const SourceRange range, llvm::StringRef newRangeSource,
                        Rewriter *rewriter) {
  int rangeSize = rewriter->getRangeSize(range);
  SourceLocation locStart = range.getBegin();
  rewriter->ReplaceText(locStart, rangeSize, newRangeSource);
}

void replaceParam(const ParmVarDecl *paramDecl, llvm::StringRef newParamSource,
                  Rewriter *rewriter) {
  SourceRange range = paramDecl->getSourceRange();
  replaceSourceRange(range, newParamSource, rewriter);
}

void addNewParam(const FunctionDecl *funcDecl, llvm::StringRef newParamSource,
                 Rewriter *rewriter) {
  // get the location and determine if we need to add a comma before the new
  // param
  // 1. after parameters we have already added (add comma)
  // 2. at the end of existing parameters list (add comma)
  // 3. before the closing brace (no comma)
  SourceLocation loc;
  bool addComma = false;

  // TODO: return if externally defined functions
  auto funcName = funcDecl->getNameAsString();
  if (funcName == "fgets" || funcName == "fgetc")
    return;

  int numParams = funcDecl->getNumParams();
  if (numParams != 0)
    addComma = true;

  // get the location, at which the body begins, this way we always add new
  // parameters at the end
  loc = funcDecl->getBody()->getLocStart().getLocWithOffset(-2);

  // check if we have already added parameters to the function
  auto addedParamsTuple = funcToAddedParameters.find(funcDecl);
  if (addedParamsTuple != funcToAddedParameters.end() &&
      addedParamsTuple->second.size() > 0)
    addComma = true;

  // insert the new parameter
  std::string paramSource = "";
  if (addComma)
    paramSource.append(", ");

  paramSource.append(newParamSource);

  rewriter->InsertTextAfter(loc, paramSource);

  // add the new parameter to the list of added parameters
  funcToAddedParameters[funcDecl].push_back(paramSource);
}

void replaceArgument(const CallExpr *call, const DeclRefExpr *oldArgument,
                     llvm::StringRef newArgument, Rewriter *rewriter) {
  // need to do a special thing, as stdin does not return a proper location
  // assuming that stdin is the last argument in the function's list
  auto argName = oldArgument->getDecl()->getNameAsString();
  if (argName == "stdin") {
    auto rangeSize = argName.length();
    auto callEnd = call->getLocEnd();
    auto locBegin = callEnd.getLocWithOffset(-1 * rangeSize);
    rewriter->ReplaceText(locBegin, rangeSize, newArgument);
    return;
  }

  auto locBegin = oldArgument->getLocStart();
  auto rangeSize = rewriter->getRangeSize(oldArgument->getSourceRange());
  rewriter->ReplaceText(locBegin, rangeSize, newArgument);
}

void addNewArgument(const CallExpr *call, llvm::StringRef newArgument,
                    Rewriter *rewriter) {
  // get the location and determine if we need to add a comma before the new Arg
  // 1. after parameters we have already added (add comma)
  // 2. at the end of existing parameters list (add comma)
  // 3. after an opening brace (no comma)
  SourceLocation loc;
  bool addComma = false;

  int numArgs = call->getNumArgs();
  if (numArgs != 0)
    addComma = true;

  loc = call->getLocEnd();

  // check if we have already added parameters to the function
  auto addedArgsTuple = funcCallToAddedArgs.find(call);
  if (addedArgsTuple != funcCallToAddedArgs.end() &&
      addedArgsTuple->second.size() > 0)
    addComma = true;

  // add comma if the function already has arguments
  std::string argSource = "";
  if (addComma)
    argSource.append(", ");

  argSource.append(newArgument);

  // insert the new parameter
  rewriter->InsertTextAfter(loc, argSource);

  // add the new argument to the list of added arguments
  funcCallToAddedArgs[call].push_back(argSource);
}

void commentOut(SourceRange range, Rewriter *rewriter) {
  rewriter->InsertText(range.getBegin(), "/*");
  rewriter->InsertText(range.getEnd().getLocWithOffset(2), "*/");
}

void commentOut(SourceLocation start, SourceLocation end, Rewriter *rewriter) {
  rewriter->InsertText(start, "/*");
  rewriter->InsertText(end.getLocWithOffset(2), "*/");
}

void insertOnNewLineAfter(SourceRange range, std::string textToInsert,
                          Rewriter *rewriter) {
  auto location = range.getEnd();
  while (rewriter->getSourceMgr().getCharacterData(location)[0] != '\n') {
    location = location.getLocWithOffset(1);
  }

  rewriter->InsertText(location, textToInsert, true, true);
}

void commentOutLine(SourceRange range, Rewriter *rewriter) {
  auto beginLoc = range.getBegin();
  while (rewriter->getSourceMgr().getCharacterData(beginLoc)[0] != '\n') {
    beginLoc = beginLoc.getLocWithOffset(-1);
  }
  beginLoc = beginLoc.getLocWithOffset(1);

  auto endLoc = range.getEnd();
  while (rewriter->getSourceMgr().getCharacterData(endLoc)[0] != '\n') {
    endLoc = endLoc.getLocWithOffset(1);
  }
  endLoc = endLoc.getLocWithOffset(-2);

  commentOut(beginLoc, endLoc, rewriter);
}

void commentOutToEndOfLine(SourceRange range, Rewriter &rewriter) {
  auto beginLoc = range.getBegin();
  rewriter.InsertText(beginLoc, "//");
}

bool isGlobalVar(const DeclRefExpr *expr) {
  for (auto &globalVar : globalVars) {
    if (expr->getDecl() == globalVar)
      return true;
  }

  return false;
}

bool isTestInput(const VarDecl *var, struct Declaration &inputRef) {
  for (auto &input : inputs) {
    if (var->getNameAsString() == input.name) {
      inputRef = input;
      return true;
    }
  }

  return false;
}

void addAssignmentForArrayTestInputs(const struct Declaration &input,
                                     std::stringstream &bbInsertion) {
  if (input.isArray) {
    bbInsertion << "  ";
    if (input.isConst)
      bbInsertion << "const ";
    bbInsertion << input.type << " *" << input.name << ";\n";
    bbInsertion << "  " << input.name << " = "
                << kernel_gen_constants::TEST_INPUT << "." << input.name
                << ";\n";
    inputsToIsAddedDeclaration[input.name] = true;
  }
}

bool containsRefToGlobalVar(const FunctionDecl *decl) {
  return funcToGlobalVars.find(decl) != funcToGlobalVars.end();
}

bool containsRefToInput(const FunctionDecl *decl) {
  return find(functionsWhichUseTestInputs.begin(),
              functionsWhichUseTestInputs.end(),
              decl) != functionsWhichUseTestInputs.end();
}

bool containsRefToOutput(const FunctionDecl *decl) {
  return find(functionsWhichUseTestOutputs.begin(),
              functionsWhichUseTestOutputs.end(),
              decl) != functionsWhichUseTestOutputs.end();
}

bool containsRefToStdin(const FunctionDecl *decl) {
  return find(functionsWhichUseStdin.begin(), functionsWhichUseStdin.end(),
              decl) != functionsWhichUseStdin.end();
}

bool isOutputPrintedChatByChar(const struct OutputDeclaration &output) {
  return output.testedValue.name == "fputc";
}

bool isMain(const FunctionDecl *decl) {
  return decl->getNameAsString() == "main";
}

// recursively add global variables to the function and all functions which call
// it
void addGlobalVarsToFunctionDecl(const FunctionDecl *funcDecl,
                                 std::vector<const ValueDecl *> globalVars) {
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if (callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;

  for (auto &callerDecl : callerDecls) {
    if (isMain(callerDecl))
      continue;

    for (auto &globalVar : globalVars) {
      // do not add a global var more than once to the same funciton
      auto addedVarsTuple = funcToGlobalVars.find(callerDecl);
      if (addedVarsTuple != funcToGlobalVars.end()) {
        auto addedVars = addedVarsTuple->second;
        if (find(addedVars.begin(), addedVars.end(), globalVar) !=
            addedVars.end())
          continue;
      }

      funcToGlobalVars[callerDecl].push_back(globalVar);
    }

    addGlobalVarsToFunctionDecl(callerDecl, globalVars);
  }
}

// recursively add inputs and outputs to the function and all functions which
// call it
void addInputAndOutputsToFunctionDecl(const FunctionDecl *funcDecl,
                                      int inputOrOutput) {
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if (callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;
  for (auto &callerDecl : callerDecls) {
    if (isMain(callerDecl))
      continue;

    switch (inputOrOutput) {
    case INPUT_1:
      if (!containsRefToInput(callerDecl))
        functionsWhichUseTestInputs.push_back(callerDecl);
      break;

    case OUTPUT_1:
      if (!containsRefToOutput(callerDecl))
        functionsWhichUseTestOutputs.push_back(callerDecl);
      break;
    }

    addInputAndOutputsToFunctionDecl(callerDecl, inputOrOutput);
  }
}

void addStdinToFunctionDecl(const FunctionDecl *funcDecl) {
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if (callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;
  for (auto &callerDecl : callerDecls) {
    if (isMain(callerDecl))
      continue;

    if (!containsRefToStdin(callerDecl))
      functionsWhichUseStdin.push_back(callerDecl);

    addStdinToFunctionDecl(callerDecl);
  }
}

// finds functions which call other functions that:
// 1. call global variables directly
// 2. use inputs and outputs
// 3. use stdin
void findAllFunctionsWhichUseSpecialVars() {
  for (auto &funcDeclTuple : funcDeclToCallerDecls) {
    auto funcDecl = funcDeclTuple.first;

    // TODO: remove doing the search in funcToGlobalVars twice
    if (containsRefToGlobalVar(funcDecl)) {
      auto globalVars = funcToGlobalVars.find(funcDecl)->second;
      addGlobalVarsToFunctionDecl(funcDecl, globalVars);
    }

    if (containsRefToInput(funcDecl))
      addInputAndOutputsToFunctionDecl(funcDecl, INPUT_1);

    if (containsRefToOutput(funcDecl))
      addInputAndOutputsToFunctionDecl(funcDecl, OUTPUT_1);

    if (containsRefToStdin(funcDecl))
      addStdinToFunctionDecl(funcDecl);
  }
}

void addInclude(std::string includeToAdd) {
  if (find(includesToAdd.begin(), includesToAdd.end(), includeToAdd) !=
      includesToAdd.end())
    return;

  includesToAdd.push_back(includeToAdd);
}

// Returns false if it cannot find the InputParam in the map
bool getInputParamFromArgvIndex(const ArraySubscriptExpr *argvExpr,
                                std::string *newText) {
  // Get the index expression
  const Expr *arrayIndex = argvExpr->getRHS();
  std::string stringExpr;
  llvm::raw_string_ostream s(stringExpr);
  arrayIndex->printPretty(s, 0, printingPolicy);

  // Is the index expression an integer?
  int index;
  std::stringstream indexss(s.str());
  if (!(indexss >> index)) {
    // TODO: Better error message
    llvm::outs() << "The below argv index is not a valid StringLiteral - we "
                    "cannot convert it to OpenCL.\n";
    arrayIndex->dump();
    llvm::outs() << "\n";
    return false;
  }

  if (argvIdxToInput[index] == "") {
    llvm::outs() << "there is no input param for argv index " << index
                 << " - cannot convert it to OpenCL.\n";
    return false;
  }

  newText->append(argvIdxToInput[index]);
  return true;
}

bool isInputArgv() {
  for (auto &input : inputs) {
    if (input.name == "argv")
      return true;
  }

  return false;
}

void subtractOneIfArgvInput(const ArraySubscriptExpr *expr,
                            Rewriter &rewriter) {
  if (!isInputArgv())
    return;

  // get the current array index expression
  const Expr *arrayIndex = expr->getRHS();
  std::string stringExpr;
  llvm::raw_string_ostream s(stringExpr);
  arrayIndex->printPretty(s, 0, printingPolicy);

  // add -1 to it
  std::stringstream newIndex;
  newIndex << s.str() << " - 1";

  // replace it
  auto arrayLoc =
      rewriter.getSourceMgr().getSpellingLoc(arrayIndex->getLocStart());
  int length = s.str().length();
  rewriter.ReplaceText(arrayLoc, length, newIndex.str());
}

/* AST Matchers & Handlers */
StatementMatcher argvMatcher =
    arraySubscriptExpr(
        hasBase(ignoringImpCasts(
            declRefExpr(to(varDecl(hasName("argv")))).bind("argvVar"))))
        .bind("argvArray");
class ArgvHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  ArgvHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    if (const ArraySubscriptExpr *expr =
            Output.Nodes.getNodeAs<clang::ArraySubscriptExpr>("argvArray")) {
      // This reference to argv is not in a atoi call
      if (!argvIdxToIsReplaced[expr->getIdx()]) {
        std::string text = kernel_gen_constants::TEST_INPUT;
        text.append(".");
        if (getInputParamFromArgvIndex(expr, &text)) {
          SourceRange range = expr->getSourceRange();
          int length = rewriter.getRangeSize(range);
          rewriter.ReplaceText(range.getBegin(), length, text);
        } else {
          // leave reference to argv, but if it is an input, then we need to
          // subtract 1 from the index expression
          subtractOneIfArgvInput(expr, rewriter);
        }
      }
    }
  }
};

auto argvInAtoiMatcher =
    callExpr(
        hasArgument(0, arraySubscriptExpr(hasBase(ignoringImpCasts(declRefExpr(
                                              to(varDecl(hasName("argv")))))))
                           .bind("argvArray")),
        callee(functionDecl(hasName("atoi"))))
        .bind("argvInAtoi");

class ArgvInAtoiHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  ArgvInAtoiHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    bool shouldReplace = true;

    // Get the input parameter to match
    std::string newText = kernel_gen_constants::TEST_INPUT;
    newText.append(".");

    if (const ArraySubscriptExpr *expr =
            Output.Nodes.getNodeAs<ArraySubscriptExpr>("argvArray")) {
      argvIdxToIsReplaced[expr->getIdx()] = true;

      shouldReplace = getInputParamFromArgvIndex(expr, &newText);
      if (!shouldReplace) {
        // leave reference to argv, but if it is an input, then we need to
        // subtract 1 from the index expression
        subtractOneIfArgvInput(expr, rewriter);
      }
    }

    if (const CallExpr *expr = Output.Nodes.getNodeAs<CallExpr>("argvInAtoi")) {
      if (shouldReplace) {
        SourceRange range = expr->getSourceRange();
        int length = rewriter.getRangeSize(range);
        rewriter.ReplaceText(range.getBegin(), length, newText);
      }
    }
  }
};

auto inputsMatcher = callExpr(hasAncestor(functionDecl().bind("inputCaller")),
                              hasAnyArgument(ignoringImpCasts(
                                  declRefExpr(to(varDecl(hasName("stdin")))))));
class InputsHandler : public MatchFinder::MatchCallback {
public:
  InputsHandler() {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const FunctionDecl *inputCaller =
        Output.Nodes.getNodeAs<FunctionDecl>("inputCaller");
    if (!isMain(inputCaller) && !containsRefToInput(inputCaller))
      functionsWhichUseTestInputs.push_back(inputCaller);
  }
};

auto outputsMatcher = callExpr(hasAncestor(functionDecl().bind("outputCaller")),
                               callee(functionDecl()))
                          .bind("outputCall");
class OutputsHandler : public MatchFinder::MatchCallback {
public:
  OutputsHandler() {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *outputCall = Output.Nodes.getNodeAs<CallExpr>("outputCall");
    for (auto &output : outputs) {
      if (outputCall->getDirectCallee()->getNameAsString() ==
          output.testedValue.name) {
        const FunctionDecl *outputCaller =
            Output.Nodes.getNodeAs<FunctionDecl>("outputCaller");
        if (!isMain(outputCaller) && !containsRefToOutput(outputCaller))
          functionsWhichUseTestOutputs.push_back(outputCaller);
      }
    }
  }
};

auto stdinMatcher =
    callExpr(hasAncestor(functionDecl().bind("stdinCaller")),
             hasAnyArgument(ignoringImpCasts(
                 declRefExpr(to(varDecl(hasName("stdin")))).bind("stdinArg"))))
        .bind("stdin");
class StdinHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  StdinHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *stdinCallExpr = Output.Nodes.getNodeAs<CallExpr>("stdin");
    const FunctionDecl *functionDecl = stdinCallExpr->getDirectCallee();
    const DeclRefExpr *stdinArgExpr =
        Output.Nodes.getNodeAs<DeclRefExpr>("stdinArg");
    const FunctionDecl *caller =
        Output.Nodes.getNodeAs<FunctionDecl>("stdinCaller");

    // add function in the list of functions
    functionsWhichUseStdin.push_back(functionDecl);

    // replace stdin with a reference to the input
    // pick the name of the stdin var from test-params
    auto stdinInput = stdinInputs.begin();
    if (stdinInput == stdinInputs.end()) {
      llvm::outs() << "There are fewer stdin parameters supplied than there "
                      "are references to stdin in the code.\n";
      return;
    }

    stdinInputs.pop_front();
    std::string inputRef;
    inputRef.append(kernel_gen_constants::TEST_INPUT);
    if (isMain(caller))
      inputRef.append(".");
    else
      inputRef.append("->");

    inputRef.append(stdinInput->name);
    replaceArgument(stdinCallExpr, stdinArgExpr, inputRef, &rewriter);
  }
};

auto scanfMatcher = callExpr(callee(functionDecl(hasName("scanf"))),
                             hasAncestor(functionDecl().bind("scanfCaller")))
                        .bind("scanf");
class ScanfHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  ScanfHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *stdinCallExpr = Output.Nodes.getNodeAs<CallExpr>("scanf");
    const FunctionDecl *functionDecl = stdinCallExpr->getDirectCallee();
    const FunctionDecl *caller =
        Output.Nodes.getNodeAs<FunctionDecl>("scanfCaller");

    // add function in the list of functions
    functionsWhichUseStdin.push_back(functionDecl);

    // stdin argument to the call of scanf
    auto stdinInput = stdinInputs.begin();
    if (stdinInput == stdinInputs.end()) {
      llvm::outs() << "There are fewer stdin parameters supplied than there "
                      "are references to stdin in the code.\n";
      return;
    }

    stdinInputs.pop_front();
    std::string inputRef = "&";
    inputRef.append(kernel_gen_constants::TEST_INPUT);
    if (isMain(caller))
      inputRef.append(".");
    else
      inputRef.append("->");

    inputRef.append(stdinInput->name);
    addNewArgument(stdinCallExpr, inputRef, &rewriter);
  }
};

auto mainMatcher = functionDecl(hasName("main")).bind("mainDecl");
class MainHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  MainHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    isMainFile = true;

    const FunctionDecl *decl = Output.Nodes.getNodeAs<FunctionDecl>("mainDecl");

    // rename and label as kernel
    std::string funcName = decl->getNameInfo().getName().getAsString();
    rewriter.ReplaceText(decl->getLocation(), funcName.length(), "main_kernel");
    rewriter.InsertTextBefore(decl->getTypeSpecStartLoc(), "__kernel ");

    // make return type 'void'
    SourceRange returnTypeRange = decl->getReturnTypeSourceRange();
    int rangeSize = decl->getReturnType().getAsString().length();
    rewriter.ReplaceText(returnTypeRange.getBegin(), rangeSize, "void");

    // change argument list
    const ParmVarDecl *paramDeclArgc = decl->getParamDecl(0);
    std::stringstream ssinput;
    ssinput << "__global struct " << structs_constants::INPUT << "* "
            << kernel_gen_constants::TEST_INPUTS;
    replaceParam(paramDeclArgc, ssinput.str(), &rewriter);

    const ParmVarDecl *paramDeclArgv = decl->getParamDecl(1);
    std::stringstream ssoutput;
    ssoutput << "__global struct " << structs_constants::OUTPUT << "* "
             << kernel_gen_constants::TEST_OUTPUTS;
    replaceParam(paramDeclArgv, ssoutput.str(), &rewriter);

    // add variables at the beginning of body
    Stmt *body = decl->getBody();
    auto bbLoc = body->getSourceRange().getBegin().getLocWithOffset(1);
    auto eLoc = body->getSourceRange().getEnd();

    std::stringstream bbInsertion;
    std::stringstream eInsertion;

    // append idx, input, argc and outputs lines
    bbInsertion << "\n\n  int " << kernel_gen_constants::THREAD_IDX
                << " = get_global_id(0);\n";
    bbInsertion << "  struct " << structs_constants::INPUT << " "
                << kernel_gen_constants::TEST_INPUT << " = "
                << kernel_gen_constants::TEST_INPUTS << "["
                << kernel_gen_constants::THREAD_IDX << "];\n";
    bbInsertion << "  __global struct " << structs_constants::OUTPUT << " *"
                << kernel_gen_constants::TEST_OUTPUT << " = &"
                << kernel_gen_constants::TEST_OUTPUTS << "["
                << kernel_gen_constants::THREAD_IDX << "];\n";
    bbInsertion << "  int " << structs_constants::ARGC << " = "
                << kernel_gen_constants::TEST_INPUT << "."
                << structs_constants::ARGC << ";\n";
    bbInsertion << "  " << kernel_gen_constants::TEST_OUTPUT << "->"
                << structs_constants::TEST_ID << " = "
                << kernel_gen_constants::TEST_INPUT << "."
                << structs_constants::TEST_ID << ";\n";

    // add declarations for global variables
    bbInsertion << "\n";
    for (auto &globalVar : globalVars) {
      // this is not a test input or it is one which isn't an array
      struct Declaration inputRef;
      if (!isTestInput(globalVar, inputRef) || !inputRef.isArray) {
        // get original declaration
        bbInsertion << "  ";
        std::string stringLiteral;
        llvm::raw_string_ostream s(stringLiteral);
        globalVar->print(s, 0, true);

        // if an array, make it private
        if (globalVar->getType()->isArrayType())
          bbInsertion << "private ";

        bbInsertion << s.str() << ";\n";
      } else {
        // If an array based test input, add assignment here
        addAssignmentForArrayTestInputs(inputRef, bbInsertion);
      }
    }

    // add assignment for array based test inputs
    for (auto &input : inputs) {
      if (!inputsToIsAddedDeclaration[input.name])
        addAssignmentForArrayTestInputs(input, bbInsertion);
    }

    // TODO: Handle multiple outputs more gracefully for fputc
    for (auto &output : outputs) {
      // add declaration for counter in case there is a output which is printed
      // char by char
      if (isOutputPrintedChatByChar(output)) {
        bbInsertion << "  int out_count_gen;\n"
                    << "  out_count_gen = 0;\n";
      }

      // character termination in case output is printed char by char
      if (isOutputPrintedChatByChar(output)) {
        eInsertion << "  *(" << kernel_gen_constants::TEST_OUTPUT << "->"
                   << output.declaration.name
                   << " + out_count_gen) = \'\\0\';\n";
      }

      // when the tested value is a variable, add an assignment to theoutput
      // struct
      if (output.testedValue.type == TestedValueType::variable) {
        if (output.declaration.isArray) {
          eInsertion << "  for(int i = 0; i < ";
          eInsertion << output.declaration.size;
          eInsertion << "; i++)\n";
          eInsertion << "  {\n";
          eInsertion << "    " << kernel_gen_constants::TEST_OUTPUT << "->";
          eInsertion << output.declaration.name;
          eInsertion << "[i] = ";
          eInsertion << output.testedValue.name;
          eInsertion << "[i];\n";
          eInsertion << "  }\n";
        } else {
          eInsertion << "  " << kernel_gen_constants::TEST_OUTPUT << "->";
          eInsertion << output.declaration.name;
          eInsertion << " = ";
          eInsertion << output.testedValue.name;
          eInsertion << ";\n";
        }
      }
    }

    // insert in the beginning
    rewriter.InsertText(bbLoc, bbInsertion.str());

    // insert in the end
    rewriter.InsertText(eLoc, eInsertion.str());
  }
};

auto returnInMainMatcher =
    returnStmt(hasAncestor(functionDecl(hasName("main")))).bind("returnInMain");
class ReturnInMainHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  ReturnInMainHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const ReturnStmt *returnStmt =
        Output.Nodes.getNodeAs<ReturnStmt>("returnInMain");
    commentOutToEndOfLine(returnStmt->getSourceRange(), rewriter);
  }
};

auto commentOutMatcher = callExpr(callee(functionDecl())).bind("commentOut");
class CommentOutHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  CommentOutHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *expr = Output.Nodes.getNodeAs<CallExpr>("commentOut");
    auto funcName = expr->getDirectCallee()->getNameAsString();

    if (funcName == "printf" || funcName == "fprintf" || funcName == "exit" ||
        funcName == "abort" || funcName.find("fput") != std::string::npos) {
      // Comment out
      auto range = expr->getSourceRange();
      commentOut(range, &rewriter);
    }
  }
};

// find variable length arrays
// turn them into constant length arrays
auto variableLengthArraysMatcher =
    varDecl(hasType(variableArrayType().bind("variableLengthArrayType")));
class VariableLengthArraysHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  VariableLengthArraysHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const VariableArrayType *type =
        Output.Nodes.getNodeAs<VariableArrayType>("variableLengthArrayType");

    SourceRange bracketsRange = type->getBracketsRange();
    std::stringstream newBracketExpr;
    newBracketExpr << "[" << structs_constants::POINTER_ARRAY_SIZE << "]";
    replaceSourceRange(bracketsRange, newBracketExpr.str(), &rewriter);
  }
};

// find global vars
// comment out declarations
// build a list of global var declarations
auto globalVarMatcher = varDecl().bind("globalVar");
class GlobalVarHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  GlobalVarHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    if (const VarDecl *decl = Output.Nodes.getNodeAs<VarDecl>("globalVar")) {
      // find out if the declaration is global
      if (decl->isFileVarDecl() && !decl->hasExternalStorage()) {
        // comment out the global variable
        auto start = decl->getLocStart();
        auto end = decl->getLocEnd();
        // for arrays and initialised vars the end is where it should be
        if (!decl->getType()->isArrayType() && !decl->getInit())
          end = end.getLocWithOffset(decl->getNameAsString().length());

        commentOut(start, end, &rewriter);

        // add it to our list of variables
        globalVars.push_back(decl);
      }
    }
  }
};

// Find all functions which use global vars directly and build a map for them
auto globalVarUseMatcher =
    declRefExpr(hasAncestor(functionDecl().bind("globalVarUseFunction")))
        .bind("globalVarUse");
class GlobalVarUseHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  GlobalVarUseHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const auto *decl =
        Output.Nodes.getNodeAs<FunctionDecl>("globalVarUseFunction");
    const auto *expr = Output.Nodes.getNodeAs<DeclRefExpr>("globalVarUse");

    // do not add new parameters to "main"
    if (isMain(decl))
      return;

    if (!isGlobalVar(expr))
      return;

    // turn the reference into a pointer
    auto varType = expr->getType();
    if (!varType->isArrayType()) {
      // gets the correct macro location when the expression is in a macro
      const auto exprLoc =
          rewriter.getSourceMgr().getSpellingLoc(expr->getLocStart());

      // this map is used when we have a macro used in multiple places so we do
      // not dereference multiple times
      if (!locationToPointerDereferenced[exprLoc]) {
        rewriter.InsertText(exprLoc, "(*");
        rewriter.InsertText(
            exprLoc.getLocWithOffset(expr->getDecl()->getNameAsString().size()),
            ")");
        locationToPointerDereferenced[exprLoc] = true;
      }
    }

    // check if the global var was already added to the argument list
    auto visitedVarsTuple = funcToGlobalVars.find(decl);
    if (visitedVarsTuple != funcToGlobalVars.end()) {
      auto visitedVars = visitedVarsTuple->second;
      if (find(visitedVars.begin(), visitedVars.end(), expr->getDecl()) !=
          visitedVars.end())
        return;
    }

    funcToGlobalVars[decl].push_back(expr->getDecl());
  }
};

// Build a callee to caller map
auto calleeToCallerMatcher =
    callExpr(hasAncestor(functionDecl().bind("caller"))).bind("callee");
class CalleeToCallerHandler : public MatchFinder::MatchCallback {
public:
  CalleeToCallerHandler() {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *callee = Output.Nodes.getNodeAs<CallExpr>("callee");
    const FunctionDecl *caller = Output.Nodes.getNodeAs<FunctionDecl>("caller");

    funcCallToCallerDecl[callee] = caller;
    auto decl = callee->getDirectCallee();
    funcDeclToCallerDecls[decl].push_back(caller);
  }
};

// Add parameters to the declarations of all functions which use global
// variables
auto globalVarsAsParamsMatcher = functionDecl().bind("globalVarsAsParams");
class GlobalVarsAsParamsHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  GlobalVarsAsParamsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    auto *decl = Output.Nodes.getNodeAs<FunctionDecl>("globalVarsAsParams");

    // do not add new parameters to "main"
    if (isMain(decl))
      return;

    auto globalVarsIt = funcToGlobalVars.find(decl);
    if (globalVarsIt == funcToGlobalVars.end())
      return;

    auto globalVars = globalVarsIt->second;
    for (auto var = globalVars.begin(); var != globalVars.end(); var++) {
      auto varName = (*var)->getNameAsString();
      auto varType = (*var)->getType();

      // add the global param to the function's argument list
      std::string newParam;
      if (varType->isArrayType()) {
        // if array, we want to take the base type only and add 'private'
        // eg. int, not int[4]
        newParam = "private ";
        newParam.append(
            varType->getAsArrayTypeUnsafe()->getElementType().getAsString());
      } else {
        newParam = varType.getAsString();
      }
      newParam.append(" ");

      // turn it into a pointer
      if (!varType->isArrayType()) {
        newParam.append("*");
      }
      newParam.append(varName);

      if (varType->isArrayType())
        newParam.append("[]");

      addNewParam(decl, newParam, &rewriter);
    }
  }
};

// Add arguments to function calls which use global vars
auto globalVarsAsArgsMatcher =
    callExpr(hasAncestor(functionDecl().bind("globalVarsAsArgsCaller")))
        .bind("globalVarsAsArgs");
class GlobalVarsAsArgsHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  GlobalVarsAsArgsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *call = Output.Nodes.getNodeAs<CallExpr>("globalVarsAsArgs");
    const FunctionDecl *caller =
        Output.Nodes.getNodeAs<FunctionDecl>("globalVarsAsArgsCaller");

    // find out if this is a call to a function which uses global vars
    auto funcDecl = call->getDirectCallee();

    auto globalVarDeclsIt = funcToGlobalVars.find(funcDecl);
    if (globalVarDeclsIt == funcToGlobalVars.end())
      return;

    // add the global var as an argument
    auto globalVars = globalVarDeclsIt->second;
    for (auto &var : globalVars) {
      std::string newArg;

      // find out if we added the global vars to the caller;
      // if we did, then do not add &, as they are already pointers
      auto callerGlobalVars = funcToGlobalVars.find(caller);
      if (callerGlobalVars == funcToGlobalVars.end()) {
        if (!var->getType()->isArrayType())
          newArg.append("&");
      }

      newArg.append(var->getNameAsString());

      addNewArgument(call, newArg, &rewriter);
    }
  }
};

// Add parameters to the declarations of all functions which use global
// variables
auto inputsAndOutputsAsParamsMatcher =
    functionDecl().bind("inputsAndOutputsAsParams");
class InputsAndOutputsAsParamsHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  InputsAndOutputsAsParamsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    auto *decl =
        Output.Nodes.getNodeAs<FunctionDecl>("inputsAndOutputsAsParams");

    // do not add new parameters to "main"
    if (isMain(decl))
      return;

    if (containsRefToInput(decl)) {
      // add the input to the function's argument list
      std::string newParam;
      newParam.append("struct ");
      newParam.append(structs_constants::INPUT);
      newParam.append(" *");
      newParam.append(kernel_gen_constants::TEST_INPUT);
      addNewParam(decl, newParam, &rewriter);
    }

    if (containsRefToOutput(decl)) {
      // add the output to the function's argument list
      std::string newParam;
      newParam.append("__global struct");
      newParam.append(structs_constants::OUTPUT);
      newParam.append(" *");
      newParam.append(kernel_gen_constants::TEST_OUTPUT);
      addNewParam(decl, newParam, &rewriter);

      // if the output is printed char by char, add a pointer to the counter
      for (auto &output : outputs) {
        if (isOutputPrintedChatByChar(output)) {
          std::string newParam2;
          newParam2.append("int *out_count_gen");
          addNewParam(decl, newParam2, &rewriter);
          break;
        }
      }
    }
  }
};

// Add arguments to function calls which use inputs and outputs
auto inputsAndOutputsAsArgsMatcher =
    callExpr(hasAncestor(functionDecl().bind("inputsAndOutputsAsArgsCaller")))
        .bind("inputsAndOutputsAsArgs");
class InputsAndOutputsAsArgsHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  InputsAndOutputsAsArgsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *call =
        Output.Nodes.getNodeAs<CallExpr>("inputsAndOutputsAsArgs");
    const FunctionDecl *caller =
        Output.Nodes.getNodeAs<FunctionDecl>("inputsAndOutputsAsArgsCaller");

    // find out if this is a call to a function which inputs and outputs
    auto funcDecl = call->getDirectCallee();

    if (isMain(funcDecl))
      return;

    // handle inputs
    if (containsRefToInput(funcDecl)) {
      // if the caller is Main, then add '&', as input isn't a pointer
      std::string newArg;
      if (isMain(caller)) {
        newArg.append("&");
      }
      newArg.append(kernel_gen_constants::TEST_INPUT);
      addNewArgument(call, newArg, &rewriter);
    }

    if (containsRefToOutput(funcDecl)) {
      // no need to add '&' in main, as output is a pointer
      std::string newArg;
      newArg.append(kernel_gen_constants::TEST_OUTPUT);
      addNewArgument(call, newArg, &rewriter);

      // if the output is printed char by char, add a pointer to the counter
      for (auto &output : outputs) {
        if (isOutputPrintedChatByChar(output)) {
          std::string newArg2;
          if (isMain(caller)) {
            newArg2.append("&");
          }
          newArg2.append("out_count_gen");
          addNewArgument(call, newArg2, &rewriter);
        }
      }
    }
  }
};

// Handle tested values which are in function calls
// auto functionToTestMatcher = callExpr(
auto testedValueFunctionCallMatcher =
    callExpr(callee(functionDecl())).bind("functionToTest");
// class FunctionToTestHandler : public MatchFinder::MatchCallback
class TestedValueFunctionCallHandler : public MatchFinder::MatchCallback {
private:
  Rewriter &rewriter;

public:
  TestedValueFunctionCallHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    for (auto &output : outputs) {
      if (output.testedValue.type != TestedValueType::functionCall)
        break;

      const CallExpr *expr = Output.Nodes.getNodeAs<CallExpr>("functionToTest");

      // find out if we are testing for a function call and this is a call to
      // the tested function
      auto name = expr->getDirectCallee()->getNameAsString();
      auto outputDecl = outputs.front();

      if (name != output.testedValue.name)
        return;

      std::string outputString;

      if (output.testedValue.name == "fputc") {
        if (outputDecl.declaration.type != "char*" &&
            outputDecl.declaration.type != "char *") {
          llvm::outs() << "The tested function is fputc, so the type of the "
                          "output in the config file should be 'char *' and "
                          "not '"
                       << outputDecl.declaration.type
                       << "'. Please change it.\n";
        }

        // get the first argument
        std::string stringExpr;
        llvm::raw_string_ostream s(stringExpr);
        expr->getArg(0)->printPretty(s, 0, printingPolicy);

        outputString.append("*(");
        outputString.append(kernel_gen_constants::TEST_OUTPUT);
        outputString.append("->");
        outputString.append(outputDecl.declaration.name);
        outputString.append(" + *out_count_gen) = ");
        outputString.append(s.str());
        outputString.append(";\n");
        outputString.append("(*out_count_gen)++");
        outputString.append(";\n");
      } else if (output.testedValue.name == "fputs") {
      }
      // TODO: add case for printf
      else {
        // user defined function
        if (output.testedValue.outputArg <= 0) {
          // we are interested in the returnoutput
          std::string callString;
          llvm::raw_string_ostream s(callString);
          expr->printPretty(s, 0, printingPolicy);

          // TODO: read outputs from test-params
          outputString = "\n";
          outputString.append(kernel_gen_constants::TEST_OUTPUT);
          outputString.append("->");
          outputString.append(outputDecl.declaration.name);
          outputString.append(" = ");
          outputString.append(s.str());

          // if the function uses global vars, we need to add them as arguments
          // to the call
          auto globalVarArgsIt = funcCallToAddedArgs.find(expr);
          if (globalVarArgsIt != funcCallToAddedArgs.end()) {
            auto globalVars = globalVarArgsIt->second;
            for (auto var = globalVars.begin(); var != globalVars.end();
                 var++) {
              auto newArg = (*var);
              int pos = outputString.size() - 1;
              outputString.insert(pos, newArg);
            }
          }
          outputString.append(";\n");
        } else {
          // we are interested in an argument of the function
          // find out which the argument is
          auto argument = expr->getArg(output.testedValue.outputArg - 1);
          std::string stringArg;
          llvm::raw_string_ostream arg(stringArg);
          argument->printPretty(arg, 0, printingPolicy);

          // assign the value of the argument after the call to theoutput
          // struct
          if (outputDecl.declaration.type.find("*")) {
            // a pointer
            // TODO: Decide on the number of iterations
            outputString.append("\nfor(int i = 0; i < ");
            outputString.append(std::to_string(structs_constants::POINTER_ARRAY_SIZE));
            outputString.append("; i++)");
            outputString.append("{\n");
            outputString.append("  *(");
            outputString.append(kernel_gen_constants::TEST_OUTPUT);
            outputString.append("->");
            outputString.append(outputDecl.declaration.name);
            outputString.append(" + i) = *(");
            outputString.append(arg.str());
            outputString.append(" + i);\n");
            outputString.append("}\n");
          } else {
            // not a pointer
            outputString = "\n";
            outputString.append(kernel_gen_constants::TEST_OUTPUT);
            outputString.append("->");
            outputString.append(outputDecl.declaration.name);
            outputString.append(" = ");
            outputString.append(arg.str());
            outputString.append(";\n");
          }
        }
      }

      auto callRange = expr->getSourceRange();
      insertOnNewLineAfter(callRange, outputString, &rewriter);
    }
  }
};

auto includesMatcher = callExpr(callee(functionDecl())).bind("includes");
class IncludesHandler : public MatchFinder::MatchCallback {
public:
  IncludesHandler() {}

  virtual void run(const MatchFinder::MatchResult &Output) {
    const CallExpr *expr = Output.Nodes.getNodeAs<CallExpr>("includes");
    auto funcName = expr->getDirectCallee()->getNameAsString();

    auto headerName = functionToHeaderFile.find(funcName);
    if (headerName == functionToHeaderFile.end())
      return;

    addInclude(headerName->second);
  }
};

/* AST Consumer */
class KernelGenClassConsumer : public clang::ASTConsumer {
private:
  MatchFinder argvMatchFinder;
  MatchFinder ioMatchFinder;
  MatchFinder variableLengthArraysMatchFinder;
  MatchFinder discoverGlobalVarsMatchFinder;
  MatchFinder rewriteGlobalVarsMatchFinder;
  MatchFinder mainMatchFinder;
  MatchFinder includesMatchFinder;

  // Handlers (in the order we run the matchers in)
  // argv
  ArgvInAtoiHandler argvInAtoiHandler;
  ArgvHandler argvHandler;

  // stdin
  StdinHandler stdinHandler;
  ScanfHandler scanfHandler;

  // I/O
  CommentOutHandler commentOutHandler;

  // variable length arrays
  VariableLengthArraysHandler variableLengthArraysHandler;

  // global vars
  GlobalVarHandler globalVarHandler;
  GlobalVarUseHandler globalVarUseHandler;
  CalleeToCallerHandler calleeToCallerHandler;
  InputsHandler inputsHandler;
  OutputsHandler outputsHandler;
  InputsAndOutputsAsParamsHandler inputsAndOutputsAsParamsHandler;
  InputsAndOutputsAsArgsHandler inputsAndOutputsAsArgsHandler;
  GlobalVarsAsParamsHandler globalVarsAsParamsHandler;
  GlobalVarsAsArgsHandler globalVarsAsArgsHandler;

  // output
  TestedValueFunctionCallHandler testedValueFunctionCallHandler;

  // main
  MainHandler mainHandler;
  ReturnInMainHandler returnInMainHandler;

  // includes
  IncludesHandler includesHandler;

public:
  KernelGenClassConsumer(Rewriter &R)
      : argvInAtoiHandler(R), argvHandler(R), stdinHandler(R), scanfHandler(R),
        commentOutHandler(R), variableLengthArraysHandler(R),
        globalVarHandler(R), globalVarUseHandler(R), calleeToCallerHandler(),
        inputsHandler(), outputsHandler(), inputsAndOutputsAsParamsHandler(R),
        inputsAndOutputsAsArgsHandler(R), globalVarsAsParamsHandler(R),
        globalVarsAsArgsHandler(R), testedValueFunctionCallHandler(R),
        mainHandler(R), returnInMainHandler(R), includesHandler() {

    argvMatchFinder.addMatcher(argvInAtoiMatcher, &argvInAtoiHandler);
    argvMatchFinder.addMatcher(argvMatcher, &argvHandler);

    ioMatchFinder.addMatcher(commentOutMatcher, &commentOutHandler);

    variableLengthArraysMatchFinder.addMatcher(variableLengthArraysMatcher,
                                               &variableLengthArraysHandler);

    discoverGlobalVarsMatchFinder.addMatcher(globalVarMatcher,
                                             &globalVarHandler);
    discoverGlobalVarsMatchFinder.addMatcher(globalVarUseMatcher,
                                             &globalVarUseHandler);
    discoverGlobalVarsMatchFinder.addMatcher(calleeToCallerMatcher,
                                             &calleeToCallerHandler);
    discoverGlobalVarsMatchFinder.addMatcher(inputsMatcher, &inputsHandler);
    discoverGlobalVarsMatchFinder.addMatcher(outputsMatcher, &outputsHandler);
    discoverGlobalVarsMatchFinder.addMatcher(stdinMatcher, &stdinHandler);
    discoverGlobalVarsMatchFinder.addMatcher(scanfMatcher, &scanfHandler);

    rewriteGlobalVarsMatchFinder.addMatcher(globalVarsAsParamsMatcher,
                                            &globalVarsAsParamsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(globalVarsAsArgsMatcher,
                                            &globalVarsAsArgsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(inputsAndOutputsAsParamsMatcher,
                                            &inputsAndOutputsAsParamsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(inputsAndOutputsAsArgsMatcher,
                                            &inputsAndOutputsAsArgsHandler);

    mainMatchFinder.addMatcher(testedValueFunctionCallMatcher,
                               &testedValueFunctionCallHandler);
    mainMatchFinder.addMatcher(mainMatcher, &mainHandler);
    mainMatchFinder.addMatcher(returnInMainMatcher, &returnInMainHandler);

    includesMatchFinder.addMatcher(includesMatcher, &includesHandler);
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    argvMatchFinder.matchAST(Context);
    ioMatchFinder.matchAST(Context);
    variableLengthArraysMatchFinder.matchAST(Context);
    discoverGlobalVarsMatchFinder.matchAST(Context);

    findAllFunctionsWhichUseSpecialVars();

    rewriteGlobalVarsMatchFinder.matchAST(Context);
    mainMatchFinder.matchAST(Context);
    includesMatchFinder.matchAST(Context);
  }
};

/* Frontend Action
 * NB: A new FrontEndAction will be created for each source file
 */
class KernelGenClassAction : public clang::ASTFrontendAction {
private:
  Rewriter rewriter;

public:
  // Write outputs in a new file
  void EndSourceFileAction() override {

    auto filename = getCurrentFile().rsplit('/').second;
    auto &sourceMgr = rewriter.getSourceMgr();

    // get the rewritebuffer and declare source for the current file
    std::string rewriteBuffer;
    std::string source;
    auto buffer = rewriter.getRewriteBufferFor(sourceMgr.getMainFileID());
    if (buffer == NULL) { // Rewrite buffer is null. No modification for file
                          // the current file."
      rewriteBuffer = sourceMgr.getBufferData(sourceMgr.getMainFileID());
    } else {
      rewriteBuffer = std::string(buffer->begin(), buffer->end());
    }

    // if file ends in .c, change to .cl
    auto filenameStr = filename.str();
    if (filename.endswith(".c")) {
      filename = filename.rsplit('.').first;
      filenameStr = filename.str() + ".cl";
    }

    // if this is the main file, change to main.cl
    if (isMainFile) {
      isMainFile = false;

      filenameStr = "main.cl";

      // include for 'structs.h' and special headers
      source = "#include \"";
      source.append(filename_constants::STRUCTS_FILENAME);
      source.append("\"\n");
      for (auto inc = includesToAdd.begin(); inc != includesToAdd.end();
           inc++) {
        source.append("#include \"");
        source.append(*inc);
        source.append("\"\n");
      }
    }

    // comment out includes and typedef bool
    std::string line;
    std::istringstream bufferStream(rewriteBuffer);
    while (getline(bufferStream, line)) {
      std::istringstream iss(line);
      std::string token1, token2;
      iss >> token1;
      iss >> token2;

      // includes (only for system headers)
      if (token1.find("#include") != std::string::npos ||
          token2.find("include") != std::string::npos) {
        if (line.find("<") != std::string::npos)
          source.append("//");
      }

      // typedef bool
      if (token1.find("typedef") != std::string::npos) {
        std::string token3;
        iss >> token3;
        if (token3.find("bool") != std::string::npos)
          source.append("//");
      }

      source.append(line);
      source.append("\n");
    }

    // Write into file
    auto outputFile = testClOutputDirectory + "/" + filenameStr;
    std::ofstream clFile;
    clFile.open(outputFile);
    clFile << source;
    clFile.close();
  }

  virtual std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &Compiler, StringRef InFile) override {
    rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    return llvm::make_unique<KernelGenClassConsumer>(rewriter);
  }
};

void generateKernel(ClangTool *_tool, std::string outputDirectory,
                    std::map<int, std::string> _argvIdxToInput,
                    std::list<struct Declaration> _inputs,
                    std::list<struct Declaration> _stdinInputs,
                    std::list<struct OutputDeclaration> _outputs) {
  llvm::outs() << "Generating kernel code... ";

  // set global scope variables
  testClOutputDirectory = outputDirectory;
  argvIdxToInput = _argvIdxToInput;
  inputs = _inputs;
  stdinInputs = _stdinInputs;
  outputs = _outputs;

  // generate the kernel code
  Rewriter rewriter;
  _tool->run(newFrontendActionFactory<KernelGenClassAction>().get());

  llvm::outs() << "DONE!\n";
  llvm::outs() << "Finished!\n";
}
