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
#include <streambuf>
#include <string>
#include <algorithm>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "Utils.h"

#define INPUT  1
#define RESULT 2

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using namespace std;

LangOptions langOpts;
PrintingPolicy printingPolicy(langOpts);

//Global scope members
string testClFilename;
map<int, string> argvIdxToInput;
map<const Expr *, bool> argvIdxToIsReplaced;
list<string> stdinInputs;
list<struct declaration> results;

//a list of all the global vars
list<const VarDecl *> globalVars;

//a map of all global vars used in a function declaration (they need to be added to the parameter list) //both original uses and added as parameters to function calls
map<const FunctionDecl *, vector<const ValueDecl *>> funcToGlobalVars;
vector<const FunctionDecl *> functionsWhichUseTestInputs;
vector<const FunctionDecl *> functionsWhichUseTestResults;
vector<const FunctionDecl *> functionsWhichUseStdin;

//a map of function calls to their caller
map<const CallExpr *, const FunctionDecl *> funcCallToCallerDecl;

//a map of function declarations to all their callers
map<const FunctionDecl*, list<const FunctionDecl *>> funcDeclToCallerDecls;

//a map which contains all parameters that were added to a function declaration, together with commas in front of them
map<const FunctionDecl *, list<string>> funcToAddedParameters;

//a map which contains all arguments that were added to a function call, together with commas in front of them
map<const CallExpr *, list<string>> funcCallToAddedArgs;

//a list of include files to add
list<string> includesToAdd;

static struct TestFunction functionToTest;

void replaceParam(
    const ParmVarDecl *paramDecl, 
    llvm::StringRef newParamSource,
    Rewriter *rewriter)
{
  SourceRange range = paramDecl->getSourceRange();
  int rangeSize = rewriter->getRangeSize(range);
  SourceLocation locStart = range.getBegin();

  rewriter->ReplaceText(locStart, rangeSize, newParamSource);
}

void addNewParam(
    const FunctionDecl *funcDecl, 
    llvm::StringRef newParamSource,
    Rewriter *rewriter)
{
  //get the location and determine if we need to add a comma before the new param
  // 1. after parameters we have already added (add comma)
  // 2. at the end of existing parameters list (add comma)
  // 3. before the closing brace (no comma)
  SourceLocation loc;
  bool addComma = false;

  //TODO: return if externally defined functions
  auto funcName = funcDecl->getNameAsString();
  if(funcName == "fgets")
    return;

  int numParams = funcDecl->getNumParams();
  if(numParams != 0)
    addComma = true;

  //get the location, at which the body begins, this way we always add new parameters at the end
  loc = funcDecl->getBody()->getLocStart().getLocWithOffset(-2);

  //check if we have already added parameters to the function
  auto addedParamsTuple = funcToAddedParameters.find(funcDecl);
  if(addedParamsTuple != funcToAddedParameters.end() && addedParamsTuple->second.size()>0)
      addComma = true;

  //insert the new parameter
  string paramSource = "";
  if(addComma)
    paramSource.append(", ");

  paramSource.append(newParamSource);

  rewriter->InsertTextAfter(loc, paramSource);

  //add the new parameter to the list of added parameters
  funcToAddedParameters[funcDecl].push_back(paramSource);
}

void replaceArgument(
    const CallExpr* call,
    const DeclRefExpr* oldArgument,
    llvm::StringRef newArgument,
    Rewriter *rewriter)
{
  //need to do a special thing, as stdin does not return a proper location
  //assuming that stdin is the last argument in the function's list
  auto argName = oldArgument->getDecl()->getNameAsString();
  if(argName == "stdin")
  {
    auto rangeSize = argName.length();
    auto callEnd = call->getLocEnd();
    auto locBegin = callEnd.getLocWithOffset(-1*rangeSize);
    rewriter->ReplaceText(locBegin, rangeSize, newArgument);
    return;
  }
  
  auto locBegin = oldArgument->getLocStart();
  auto rangeSize = rewriter->getRangeSize(oldArgument->getSourceRange());
  rewriter->ReplaceText(locBegin, rangeSize, newArgument);
}

void addNewArgument(
    const CallExpr *call,
    llvm::StringRef newArgument,
    Rewriter *rewriter)
{
  //get the location and determine if we need to add a comma before the new Arg
  // 1. after parameters we have already added (add comma)
  // 2. at the end of existing parameters list (add comma)
  // 3. after an opening brace (no comma)
  SourceLocation loc;
  bool addComma = false;

  int numArgs = call->getNumArgs();
  if(numArgs != 0)
    addComma = true;

  loc = call->getLocEnd();
  
  //check if we have already added parameters to the function
  auto addedArgsTuple = funcCallToAddedArgs.find(call);
  if(addedArgsTuple!= funcCallToAddedArgs.end() && addedArgsTuple->second.size()>0)
      addComma = true;

  //add comma if the function already has arguments 
  string argSource = "";
  if(addComma)
    argSource.append(", ");

  argSource.append(newArgument);

  //insert the new parameter
  rewriter->InsertTextAfter(loc, argSource);

  //add the new argument to the list of added arguments
  funcCallToAddedArgs[call].push_back(argSource);
}


void commentOut(SourceRange range, Rewriter *rewriter)
{
  rewriter->InsertText(range.getBegin(), "/*");
  rewriter->InsertText(range.getEnd().getLocWithOffset(2), "*/");
}

void commentOut(SourceLocation start, SourceLocation end, Rewriter *rewriter)
{
  rewriter->InsertText(start, "/*");
  rewriter->InsertText(end.getLocWithOffset(2), "*/");
}

void insertOnNewLineAfter(
    SourceRange range,
    string textToInsert,
    Rewriter *rewriter)
{
  auto location = range.getEnd();
  while(rewriter->getSourceMgr().getCharacterData(location)[0] != '\n')
  {
    location = location.getLocWithOffset(1);
  }

  rewriter->InsertText(location, textToInsert, true, true);
}

void commentOutLine(SourceRange range, Rewriter *rewriter)
{
  auto beginLoc = range.getBegin();
  while(rewriter->getSourceMgr().getCharacterData(beginLoc)[0] != '\n')
  {
    beginLoc= beginLoc.getLocWithOffset(-1);
  }
  beginLoc = beginLoc.getLocWithOffset(1);

  auto endLoc= range.getEnd();
  while(rewriter->getSourceMgr().getCharacterData(endLoc)[0] != '\n')
  {
    endLoc = endLoc.getLocWithOffset(1);
  }
  endLoc = endLoc.getLocWithOffset(-2);

  commentOut(beginLoc, endLoc, rewriter);
}

bool isGlobalVar(const DeclRefExpr *expr)
{
  for(auto it = globalVars.begin(); it != globalVars.end(); it++)
  {
    if(expr->getDecl() == (*it))
      return true;
  }

  return false;
}

bool containsRefToGlobalVar(const FunctionDecl *decl)
{
  return funcToGlobalVars.find(decl) != funcToGlobalVars.end();
}

bool containsRefToInput(const FunctionDecl *decl)
{
  return find(functionsWhichUseTestInputs.begin(), functionsWhichUseTestInputs.end(), decl) != functionsWhichUseTestInputs.end();
}

bool containsRefToResult(const FunctionDecl *decl)
{
  return find(functionsWhichUseTestResults.begin(), functionsWhichUseTestResults.end(), decl) != functionsWhichUseTestResults.end();
}

bool containsRefToStdin(const FunctionDecl *decl)
{
  return find(functionsWhichUseStdin.begin(), functionsWhichUseStdin.end(), decl) != functionsWhichUseStdin.end();
}

bool isResultPrintedChatByChar()
{
  return functionToTest.name == "fputc";
}

bool isStdinUsed()
{
  return functionsWhichUseStdin.size() > 0;
}

bool isMain(const FunctionDecl *decl)
{
  return decl->getNameAsString() == "main";
}

//recursively add global variables to the function and all functions which call it
void addGlobalVarsToFunctionDecl(
    const FunctionDecl * funcDecl, 
    vector<const ValueDecl *> globalVars)
{
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if(callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;
  for(auto& callerDecl : callerDecls)
  {
    if(isMain(callerDecl))
      continue;

    for(auto& globalVar : globalVars)
    {
      //do not add a global var more than once to the same funciton
      auto addedVarsTuple = funcToGlobalVars.find(callerDecl);
      if(addedVarsTuple != funcToGlobalVars.end())
      {
        auto addedVars = addedVarsTuple->second;
        if(find(addedVars.begin(), addedVars.end(), globalVar) != addedVars.end())
          continue;
      }

      funcToGlobalVars[callerDecl].push_back(globalVar);
    }

    addGlobalVarsToFunctionDecl(callerDecl, globalVars);
  }
}

//recursively add inputs and results to the function and all functions which call it
void addInputAndResultsToFunctionDecl(
    const FunctionDecl* funcDecl,
    int inputOrResult)
{
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if(callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;
  for(auto& callerDecl : callerDecls)
  {
    if(isMain(callerDecl))
      continue;

    switch(inputOrResult)
    {
      case INPUT:
        if(!containsRefToInput(callerDecl))
          functionsWhichUseTestInputs.push_back(callerDecl);
        break;

      case RESULT:
        if(!containsRefToResult(callerDecl))
          functionsWhichUseTestResults.push_back(callerDecl);
        break;
    }

    addInputAndResultsToFunctionDecl(callerDecl, inputOrResult);
  }
}

void addStdinToFunctionDecl(const FunctionDecl *funcDecl)
{
  auto callerDeclsTuple = funcDeclToCallerDecls.find(funcDecl);
  if(callerDeclsTuple == funcDeclToCallerDecls.end())
    return;

  auto callerDecls = callerDeclsTuple->second;
  for(auto& callerDecl : callerDecls)
  {
    if(isMain(callerDecl))
      continue;

    if(!containsRefToStdin(callerDecl))
      functionsWhichUseStdin.push_back(callerDecl);

    addStdinToFunctionDecl(callerDecl);
  }
}

//builds the call graph from callee - caller map
void buildCallGraph()
{
  for(auto funcCall = funcCallToCallerDecl.begin(); funcCall != funcCallToCallerDecl.end(); funcCall++)
  {
    auto funcDecl = funcCall->first->getDirectCallee();
    funcDeclToCallerDecls[funcDecl].push_back(funcCall->second);
  }
}

//finds functions which call other functions that:
// 1. call global variables directly
// 2. use inputs and results
// 3. use stdin
void findAllFunctionsWhichUseSpecialVars()
{
  for(auto& funcDeclTuple : funcDeclToCallerDecls)
  {
    auto funcDecl = funcDeclTuple.first;

    //TODO: remove doing the search in funcToGlobalVars twice
    if(containsRefToGlobalVar(funcDecl))
    {
      auto globalVars = funcToGlobalVars.find(funcDecl)->second;
      addGlobalVarsToFunctionDecl(funcDecl, globalVars);
    }

    if(containsRefToInput(funcDecl))
      addInputAndResultsToFunctionDecl(funcDecl, INPUT);

    if(containsRefToResult(funcDecl))
      addInputAndResultsToFunctionDecl(funcDecl, RESULT);

    if(containsRefToStdin(funcDecl))
      addStdinToFunctionDecl(funcDecl);
  }
}

void addInclude(string includeToAdd)
{
  if(find(includesToAdd.begin(), includesToAdd.end(), includeToAdd) != includesToAdd.end())
    return;

  includesToAdd.push_back(includeToAdd);
}

//Returns false if it cannot find the InputParam in the map
bool getInputParamFromArgvIndex(const ArraySubscriptExpr *argvExpr, string *newText)
{
      //Get the index expression
      const Expr *arrayIndex = argvExpr->getRHS();
      int index = ((IntegerLiteral*)(arrayIndex))->getValue().getLimitedValue();

      if(index < 0)
      {
        //TODO: Better error message
        llvm::outs() << "the below argv index is not a valid StringLiteral - we cannot convert it to OpenCL.\n";
        arrayIndex -> dump();
        llvm::outs() << "\n";
        return false;
      }

      if(argvIdxToInput[index] == "")
      {
        llvm::outs() << "there is no input param for argv index " << index << " - cannot convert it to OpenCL.\n";
        return false;
      }

      newText->append(argvIdxToInput[index]);
      return true;
}

/* AST Matchers & Handlers */
StatementMatcher argvMatcher = arraySubscriptExpr(hasBase(ignoringImpCasts(declRefExpr(to(varDecl(hasName("argv")))).bind("argvVar")))).bind("argvArray");
class ArgvHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  ArgvHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    if(const ArraySubscriptExpr *expr = Result.Nodes.getNodeAs<clang::ArraySubscriptExpr>("argvArray"))
    {
      //This reference to argv is not in a atoi call
      if(!argvIdxToIsReplaced[expr->getIdx()])
      {
        string text = "input_gen.";
        if(getInputParamFromArgvIndex(expr, &text))
        {
          SourceRange range = expr->getSourceRange();
          int length = rewriter.getRangeSize(range);
          rewriter.ReplaceText(range.getBegin(), length, text);
        }
      }
    }
  }
};

auto argvInAtoiMatcher = callExpr(
    hasAnyArgument(arraySubscriptExpr(hasBase(ignoringImpCasts(declRefExpr(to(varDecl(hasName("argv"))))))).bind("argvArray")),
    callee(functionDecl(hasName("atoi")))
    ).bind("argvInAtoi");


class ArgvInAtoiHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  ArgvInAtoiHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    bool shouldReplace = true;

    //Get the input parameter to match
    string newText = "input_gen.";
    
    if(const ArraySubscriptExpr *expr = Result.Nodes.getNodeAs<ArraySubscriptExpr>("argvArray"))
    {
      argvIdxToIsReplaced[expr->getIdx()] = true;

      shouldReplace = getInputParamFromArgvIndex(expr, &newText);
    }

    if(const CallExpr *expr = Result.Nodes.getNodeAs<CallExpr>("argvInAtoi"))
    {
      if(shouldReplace)
      {
        SourceRange range = expr->getSourceRange();
        int length = rewriter.getRangeSize(range);
        rewriter.ReplaceText(range.getBegin(), length, newText);
      }
    }
  }
};

auto inputsMatcher = callExpr(hasAncestor(functionDecl().bind("inputCaller")), 
                              hasAnyArgument(ignoringImpCasts(declRefExpr(to(varDecl(hasName("stdin")))))));
class InputsHandler : public MatchFinder::MatchCallback
{
public:
  InputsHandler() {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const FunctionDecl *inputCaller = Result.Nodes.getNodeAs<FunctionDecl>("inputCaller");
    if(!isMain(inputCaller) && !containsRefToInput(inputCaller))
      functionsWhichUseTestInputs.push_back(inputCaller);
  }
};

auto resultsMatcher = callExpr(hasAncestor(functionDecl().bind("resultCaller")), callee(functionDecl())).bind("resultCall");
class ResultsHandler : public MatchFinder::MatchCallback
{
public:
  ResultsHandler() {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *resultCall = Result.Nodes.getNodeAs<CallExpr>("resultCall");
    if(resultCall->getDirectCallee()->getNameAsString() == functionToTest.name)
    {
      const FunctionDecl *resultCaller = Result.Nodes.getNodeAs<FunctionDecl>("resultCaller");
      if(!isMain(resultCaller) && !containsRefToResult(resultCaller))
        functionsWhichUseTestResults.push_back(resultCaller);
    }
  }
};


auto stdinMatcher = callExpr(hasAncestor(functionDecl().bind("stdinCaller")),
    hasAnyArgument(ignoringImpCasts(declRefExpr(to(varDecl(hasName("stdin")))).bind("stdinArg")))).bind("stdin");
class StdinHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  StdinHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *stdinCallExpr = Result.Nodes.getNodeAs<CallExpr>("stdin");
    const FunctionDecl *functionDecl = stdinCallExpr->getDirectCallee();
    const DeclRefExpr *stdinArgExpr = Result.Nodes.getNodeAs<DeclRefExpr>("stdinArg"); 
    const FunctionDecl *caller = Result.Nodes.getNodeAs<FunctionDecl>("stdinCaller"); 
    
    //add function in the list of functions
      functionsWhichUseStdin.push_back(functionDecl);

    //replace stdin with a reference to the input
    //pick the name of the stdin var from test-params
    auto stdinInput = stdinInputs.begin();
    if(stdinInput == stdinInputs.end())
    {
      llvm::outs() << "There are fewer stdin parameters supplied than there are references to stdin in the code.\n";
      return; 
    } 
    
    stdinInputs.pop_front(); 
    string inputRef; 
    if(isMain(caller))
      inputRef.append("input_gen.");
    else
      inputRef.append("(*input_gen)."); 

    inputRef.append(*stdinInput); 
    replaceArgument(stdinCallExpr, stdinArgExpr, inputRef, &rewriter); 

    /* commentOutLine(stdinExpr->getSourceRange(), &rewriter); 
    string newSource;

    string stdinString;
    llvm::raw_string_ostream s(stdinString);
    stdinExpr->getArg(0)->printPretty(s, 0, printingPolicy);

    newSource.append("int i_gen = 0;\n");
    newSource.append("while(*(");
    newSource.append(inputRef);
    newSource.append("+i_gen) != \'\\0\')\n");
    newSource.append("{\n");
    newSource.append("  *(");
    newSource.append(s.str());
    newSource.append("+i_gen) = *(");
    newSource.append(inputRef);
    newSource.append("+i_gen);\n");
    newSource.append("  i_gen++;\n");
    newSource.append("}\n");
    newSource.append("*(");
    newSource.append(s.str());
    newSource.append("+i_gen) = \'\\0\';\n");

    insertOnNewLineAfter(stdinExpr->getSourceRange(), newSource, &rewriter);
    */
  }
};

auto mainMatcher = functionDecl(hasName("main")).bind("mainDecl");
class MainHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  MainHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const FunctionDecl *decl = Result.Nodes.getNodeAs<FunctionDecl>("mainDecl");
  
    //rename and label as kernel
    string funcName = decl->getNameInfo().getName().getAsString();
    rewriter.ReplaceText(decl->getLocation(), funcName.length(), "main_kernel");
    rewriter.InsertTextBefore(decl->getTypeSpecStartLoc(), "__kernel ");

    //make return type 'void'
    SourceRange returnTypeRange = decl->getReturnTypeSourceRange();
    int rangeSize = decl->getReturnType().getAsString().length();
    rewriter.ReplaceText(returnTypeRange.getBegin(), rangeSize, "void");

    //change argument list
    const ParmVarDecl *paramDeclArgc = decl->getParamDecl(0);
    replaceParam(paramDeclArgc, "__global struct input * inputs", &rewriter);

    const ParmVarDecl *paramDeclArgv = decl->getParamDecl(1);
    replaceParam(paramDeclArgv, "__global struct result * results", &rewriter);

    //add variables at the beginning of body
    Stmt *body = decl->getBody();
    SourceLocation bbLoc = body->getSourceRange().getBegin().getLocWithOffset(1);
    string idxLine = "\n  int idx = get_global_id(0);\n";
    string inputLine = "  struct input input_gen = inputs[idx];\n";
    string resultLine1 = "  __global struct result *result_gen = &results[idx];\n";
    string argcLine = "  int argc = input_gen.argc;\n";
    string resultLine2 = "  (*result_gen).test_case_num = input_gen.test_case_num;\n";
    string insertion = "";
    insertion.append(idxLine);
    insertion.append(inputLine);
    insertion.append(resultLine1);
    insertion.append(argcLine);
    insertion.append(resultLine2);

    //add declarations for global variables
    insertion.append("\n");
    for(auto it = globalVars.begin(); it != globalVars.end(); it++)
    {
      insertion.append("  ");
      string stringLiteral;
      llvm::raw_string_ostream s(stringLiteral);
      (*it)->print(s, 0, true);
      insertion.append(s.str());
        insertion.append(";\n");
    }

    //add declaration for counter in case result is printed char by char
    if(isResultPrintedChatByChar())
    {
      insertion.append("  int res_count_gen;\n");
      insertion.append("  res_count_gen = 0;\n");
    }

    //add declaration for counter in case stdin stream is used
    if(isStdinUsed())
    {
      insertion.append("  int stdin_count_gen;\n");
      insertion.append("  stdin_count_gen = 0;\n");
    }

    rewriter.InsertText(bbLoc, insertion); 

    //character termination in case result is printed char by char
    if(isResultPrintedChatByChar())
    {
      auto eLoc = body->getSourceRange().getEnd();
      string eInsertion;
      eInsertion.append("  *((*result_gen).result + res_count_gen) = \'\\0\';\n");
      rewriter.InsertText(eLoc, eInsertion); 
    }
  }
};

auto returnInMainMatcher = returnStmt(hasAncestor(functionDecl(hasName("main")))).bind("returnInMain");
class ReturnInMainHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  ReturnInMainHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const ReturnStmt *returnStmt = Result.Nodes.getNodeAs<ReturnStmt>("returnInMain");
    commentOut(returnStmt->getSourceRange(), &rewriter);
  }
};

auto commentOutMatcher = callExpr(callee(functionDecl())).bind("commentOut");
class CommentOutHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  CommentOutHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *expr = Result.Nodes.getNodeAs<CallExpr>("commentOut");
    auto funcName = expr->getDirectCallee()->getNameAsString();
    
    if(funcName == "printf" ||
       funcName == "fprintf" || 
       funcName == "exit" ||
       funcName == "abort" ||
       funcName.find("fput") != string::npos)
    {
      //Comment out 
      auto range = expr->getSourceRange();
      commentOut(range, &rewriter);
    }
  }
};



//find global vars
//comment out declarations
//build a list of global var declarations
auto globalVarMatcher = varDecl().bind("globalVar");
class GlobalVarHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  GlobalVarHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    if(const VarDecl *decl = Result.Nodes.getNodeAs<VarDecl>("globalVar"))
    {
      //find out if the declaration is global
      if(decl->isFileVarDecl() && !decl->hasExternalStorage())
      {
        //comment out the global variable
        auto start = decl->getLocStart();
        auto end = decl->getLocEnd();
        if(!decl->getType()->isArrayType())
          end = end.getLocWithOffset(decl->getNameAsString().length());

        commentOut(start, end, &rewriter);

        //add it to our list of variables
        globalVars.push_back(decl);
      }
    }
  }
};

//Find all functions which use global vars directly and build a map for them
auto globalVarUseMatcher = declRefExpr(hasAncestor(functionDecl().bind("globalVarUseFunction"))).bind("globalVarUse");
class GlobalVarUseHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  GlobalVarUseHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const auto *decl = Result.Nodes.getNodeAs<FunctionDecl>("globalVarUseFunction");
    const auto *expr = Result.Nodes.getNodeAs<DeclRefExpr>("globalVarUse");

    //do not add new parameters to "main"
    if(isMain(decl))
      return;

    if(!isGlobalVar(expr))
      return;

    //turn the reference into a pointer
    auto varType = expr->getType();
    if(!varType->isPointerType() && !varType->isArrayType())
    {
      rewriter.InsertText(expr->getLocStart(), "*");
    }

    //check if the global var was already added to the argument list
    auto visitedVarsTuple = funcToGlobalVars.find(decl);
    if(visitedVarsTuple != funcToGlobalVars.end())
    {
      auto visitedVars = visitedVarsTuple->second;
      if(find(visitedVars.begin(), visitedVars.end(), expr->getDecl()) != visitedVars.end())
        return;
    } 

    funcToGlobalVars[decl].push_back(expr->getDecl());
  }
};

//Build a callee to caller map
auto calleeToCallerMatcher = callExpr(hasAncestor(functionDecl().bind("caller"))).bind("callee");
class CalleeToCallerHandler : public MatchFinder::MatchCallback
{
public:
  CalleeToCallerHandler() {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *callee = Result.Nodes.getNodeAs<CallExpr>("callee");
    const FunctionDecl *caller = Result.Nodes.getNodeAs<FunctionDecl>("caller");

    funcCallToCallerDecl[callee] = caller;
  }
};

//Add parameters to the declarations of all functions which use global variables
auto globalVarsAsParamsMatcher = functionDecl().bind("globalVarsAsParams");
class GlobalVarsAsParamsHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  GlobalVarsAsParamsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    auto *decl = Result.Nodes.getNodeAs<FunctionDecl>("globalVarsAsParams");

    //do not add new parameters to "main"
    if(isMain(decl))
      return;

    auto globalVarsIt = funcToGlobalVars.find(decl);
    if(globalVarsIt == funcToGlobalVars.end())
      return;

    auto globalVars = globalVarsIt->second;
    for(auto var = globalVars.begin(); var != globalVars.end(); var++)
    {
      auto varName = (*var)->getNameAsString();
      auto varType = (*var)->getType();

      //add the global param to the function's argument list
      string newParam;
      if(varType->isArrayType())
      {
        //if array, we want to take the base type only
        //eg. int, not int[4]
        newParam = varType->getAsArrayTypeUnsafe()->getElementType().getAsString();
      }
      else
      {
        newParam = varType.getAsString();
      }
      newParam.append(" ");

      //turn it into a pointer
      if(!varType->isPointerType() && !varType->isArrayType())
      {
        newParam.append("*");
      }

      newParam.append(varName);

      if(varType->isArrayType())
        newParam.append("[]");

      addNewParam(decl, newParam, &rewriter);
    }
  }
};

//Add arguments to function calls which use global vars
auto globalVarsAsArgsMatcher = callExpr(hasAncestor(functionDecl().bind("globalVarsAsArgsCaller"))).bind("globalVarsAsArgs");
class GlobalVarsAsArgsHandler: public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  GlobalVarsAsArgsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *call = Result.Nodes.getNodeAs<CallExpr>("globalVarsAsArgs");
    const FunctionDecl *caller = Result.Nodes.getNodeAs<FunctionDecl>("globalVarsAsArgsCaller");

    //find out if this is a call to a function which uses global vars
    auto funcDecl = call->getDirectCallee();

    auto globalVarDeclsIt = funcToGlobalVars.find(funcDecl);
    if(globalVarDeclsIt == funcToGlobalVars.end())
      return;

    //add the global var as an argument
    auto globalVars = globalVarDeclsIt->second;
    for(auto var = globalVars.begin(); var != globalVars.end(); var++)
    {
      string newArg;

      //find out if we added the global vars to the caller; 
      //if we did, then do not add &, as they are already pointers
      auto callerGlobalVars = funcToGlobalVars.find(caller);
      if(callerGlobalVars == funcToGlobalVars.end())
      {
        auto varType = (*var)->getType();
        if(!varType->isPointerType() && !varType->isArrayType())
          newArg.append("&");
      }

      newArg.append((*var)->getNameAsString());

      addNewArgument(call, newArg, &rewriter);
    }
  }
};

//Add parameters to the declarations of all functions which use global variables
auto inputsAndResultsAsParamsMatcher = functionDecl().bind("inputsAndResultsAsParams");
class InputsAndResultsAsParamsHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  InputsAndResultsAsParamsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    auto *decl = Result.Nodes.getNodeAs<FunctionDecl>("inputsAndResultsAsParams");

    //do not add new parameters to "main"
    if(isMain(decl))
      return;

    if(containsRefToInput(decl))
    {
      //add the input to the function's argument list
      string newParam;
      newParam.append("struct input *input_gen");
      addNewParam(decl, newParam, &rewriter);
    }

    if(containsRefToResult(decl))
    {
      //add the result to the function's argument list
      string newParam;
      newParam.append("__global struct result *result_gen");
      addNewParam(decl, newParam, &rewriter);

      //if the result is printed char by char, add a pointer to the counter
      if(isResultPrintedChatByChar())
      {
        string newParam2;
        newParam2.append("int *res_count_gen");
        addNewParam(decl, newParam2, &rewriter);
      }
    }
  }
};

//Add arguments to function calls which use inputs and results
auto inputsAndResultsAsArgsMatcher = callExpr(hasAncestor(functionDecl().bind("inputsAndResultsAsArgsCaller"))).bind("inputsAndResultsAsArgs");
class InputsAndResultsAsArgsHandler: public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  InputsAndResultsAsArgsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *call = Result.Nodes.getNodeAs<CallExpr>("inputsAndResultsAsArgs");
    const FunctionDecl *caller = Result.Nodes.getNodeAs<FunctionDecl>("inputsAndResultsAsArgsCaller");

    //find out if this is a call to a function which inputs and results 
    auto funcDecl = call->getDirectCallee();

    if(isMain(funcDecl))
      return;

    //handle inputs
    if(containsRefToInput(funcDecl))
    {
      //if the caller is Main, then add '&', as input isn't a pointer
      string newArg;
      if(isMain(caller))
      {
        newArg.append("&");
      }
      newArg.append("input_gen");
      addNewArgument(call, newArg, &rewriter);
    }

    if(containsRefToResult(funcDecl))
    {
      //no need to add '&' in main, as result is a pointer
      string newArg;
      newArg.append("result_gen");
      addNewArgument(call, newArg, &rewriter);

      //if the result is printed char by char, add a pointer to the counter
      if(isResultPrintedChatByChar())
      {
        string newArg2;
        if(isMain(caller))
        {
          newArg2.append("&");
        }
        newArg2.append("res_count_gen");
        addNewArgument(call, newArg2, &rewriter);
      }
    }
  }
};

auto stdinAsArgsMatcher = callExpr(hasAncestor(functionDecl().bind("stdinAsArgsCaller"))).bind("stdinAsArgs");
class StdinAsArgsHandler: public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  StdinAsArgsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *call = Result.Nodes.getNodeAs<CallExpr>("stdinAsArgs");
    const FunctionDecl *caller = Result.Nodes.getNodeAs<FunctionDecl>("stdinAsArgsCaller");

    //find out if this is a call to a function which inputs and results 
    auto funcDecl = call->getDirectCallee();

    if(isMain(funcDecl))
      return;

    if(containsRefToStdin(funcDecl))
    {
      string newArg;
      if(isMain(caller))
      {
        newArg.append("&");
      }
      newArg.append("stdin_count_gen");
      addNewArgument(call, newArg, &rewriter);
    }
  }
};

auto stdinAsParamsMatcher = functionDecl().bind("stdinAsParams");
class StdinAsParamsHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  StdinAsParamsHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    auto *decl = Result.Nodes.getNodeAs<FunctionDecl>("stdinAsParams");

    //do not add new parameters to "main"
    if(isMain(decl))
      return;

    if(containsRefToStdin(decl))
    {
      //add the result to the function's argument list
      string newParam;
      newParam.append("int *stdin_count_gen");
      addNewParam(decl, newParam, &rewriter);
    }
  }
};


//We cannot pass in a runtime function name, so we check for it in the handler 
//Add arguments to the function call if it uses global vars
auto functionToTestMatcher = callExpr(
    callee(functionDecl())).bind("functionToTest");
class FunctionToTestHandler : public MatchFinder::MatchCallback
{
private:
  Rewriter &rewriter;
public:
  FunctionToTestHandler(Rewriter &rewrite) : rewriter(rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *expr = Result.Nodes.getNodeAs<CallExpr>("functionToTest");

    //find out if this is a call to the tested function
    auto name = expr -> getDirectCallee() -> getNameAsString();
    if(name != functionToTest.name)
      return;
      
    auto resultDecl = results.front();
    string resultString;

    if(functionToTest.name == "fputc")
    {
      if(resultDecl.type != "char*" && resultDecl.type != "char *")
      {
        llvm::outs() << "The tested function is fputc, so the type of the result in the config file should be 'char *' and not '" << resultDecl.type << "'. Please change it.\n";
      }

      //get the first argument
      string stringExpr;
      llvm::raw_string_ostream s(stringExpr);
      expr->getArg(0)->printPretty(s, 0, printingPolicy);

      resultString.append("*((*result_gen).");
      resultString.append(resultDecl.name);
      resultString.append(" + *res_count_gen) = ");
      resultString.append(s.str());
      resultString.append(";\n");
      resultString.append("(*res_count_gen)++");
      resultString.append(";\n");
    }
    else if(functionToTest.name == "fputs")
    {
    }
    //TODO: add case for printf
    else
    {
      //user defined function
      if(functionToTest.resultArg <= 0)
      {
        //we are interested in the return result
        string callString;
        llvm::raw_string_ostream s(callString);
        expr->printPretty(s, 0, printingPolicy);

        //TODO: read results from test-params
        resultString = "\n(*result_gen).";
        resultString.append(resultDecl.name);
        resultString.append(" = ");
        resultString.append(s.str());

        //if the function uses global vars, we need to add them as arguments to the call
        auto globalVarArgsIt = funcCallToAddedArgs.find(expr);
        if(globalVarArgsIt != funcCallToAddedArgs.end())
        {
          auto globalVars = globalVarArgsIt->second;
          for(auto var = globalVars.begin(); var != globalVars.end(); var++)
          {
            auto newArg = (*var);
            int pos = resultString.size()-1;
            resultString.insert(pos, newArg);
          }
        }
        resultString.append(";\n");
      }
      else
      {
        //we are interested in an argument of the function
        //find out which the argument is
        auto argument = expr->getArg(functionToTest.resultArg-1);
        string stringArg;
        llvm::raw_string_ostream arg(stringArg);
        argument->printPretty(arg, 0, printingPolicy);

        //assign the value of the argument after the call to the result struct
        if(resultDecl.type.find("*"))
        {
          //a pointer
          //TODO: Decide on the number of iterations
          resultString.append("\nfor(int i = 0; i < 500; i++)");
          resultString.append("{\n");
          resultString.append("  *((*result_gen).");
          resultString.append(resultDecl.name);
          resultString.append(" + i) = *(");
          resultString.append(arg.str());
          resultString.append(" + i);\n");
          resultString.append("}\n");
        }
        else
        {
          //not a pointer
          resultString = "\n(*result_gen).";
          resultString.append(resultDecl.name);
          resultString.append(" = ");
          resultString.append(arg.str());
          resultString.append(";\n");
        }
      }
    }

    auto callRange = expr->getSourceRange();
    insertOnNewLineAfter(callRange, resultString, &rewriter);
  }
};

auto includesMatcher = callExpr(callee(functionDecl())).bind("includes");
class IncludesHandler : public MatchFinder::MatchCallback
{
public:
  IncludesHandler() {}

  virtual void run(const MatchFinder::MatchResult &Result)
  {
    const CallExpr *expr = Result.Nodes.getNodeAs<CallExpr>("includes");
    auto funcName = expr->getDirectCallee()->getNameAsString();

    auto headerName = functionToHeaderFile.find(funcName);
    if(headerName == functionToHeaderFile.end())
      return;

    addInclude(headerName->second);
  }
};
 

/* AST Consumer */
class KernelGenClassConsumer : public clang::ASTConsumer
{
private:
  MatchFinder argvMatchFinder;
  MatchFinder ioMatchFinder;
  MatchFinder discoverGlobalVarsMatchFinder;
  MatchFinder rewriteGlobalVarsMatchFinder;
  MatchFinder mainMatchFinder;
  MatchFinder includesMatchFinder;

  //Handlers (in the order we run the matchers in)
  //argv
  ArgvInAtoiHandler argvInAtoiHandler;
  ArgvHandler argvHandler;

  //stdin
  StdinHandler stdinHandler;
  StdinAsParamsHandler stdinAsParamsHandler;
  StdinAsArgsHandler stdinAsArgsHandler;
  
  //I/O
  CommentOutHandler commentOutHandler;

  //global vars
  GlobalVarHandler globalVarHandler;
  GlobalVarUseHandler globalVarUseHandler;
  CalleeToCallerHandler calleeToCallerHandler;
  InputsHandler inputsHandler;
  ResultsHandler resultsHandler;
  InputsAndResultsAsParamsHandler inputsAndResultsAsParamsHandler;
  InputsAndResultsAsArgsHandler inputsAndResultsAsArgsHandler;
  GlobalVarsAsParamsHandler globalVarsAsParamsHandler;
  GlobalVarsAsArgsHandler globalVarsAsArgsHandler;

  //result
  FunctionToTestHandler functionToTestHandler;

  //main
  MainHandler mainHandler;
  ReturnInMainHandler returnInMainHandler;

  //includes
  IncludesHandler includesHandler;
  
public:
  KernelGenClassConsumer(Rewriter &R) : 
    argvInAtoiHandler(R), 
    argvHandler(R), 
    stdinHandler(R),
    stdinAsParamsHandler(R),
    stdinAsArgsHandler(R),
    commentOutHandler(R),
    globalVarHandler(R),
    globalVarUseHandler(R),
    calleeToCallerHandler(),
    inputsHandler(),
    resultsHandler(),
    inputsAndResultsAsParamsHandler(R),
    inputsAndResultsAsArgsHandler(R),
    globalVarsAsParamsHandler(R),
    globalVarsAsArgsHandler(R),
    functionToTestHandler(R), 
    mainHandler(R),
    returnInMainHandler(R),
    includesHandler()
  {

    argvMatchFinder.addMatcher(argvInAtoiMatcher, &argvInAtoiHandler);
    argvMatchFinder.addMatcher(argvMatcher, &argvHandler);

    ioMatchFinder.addMatcher(commentOutMatcher, &commentOutHandler);

    discoverGlobalVarsMatchFinder.addMatcher(globalVarMatcher, &globalVarHandler);
    discoverGlobalVarsMatchFinder.addMatcher(globalVarUseMatcher, &globalVarUseHandler);
    discoverGlobalVarsMatchFinder.addMatcher(calleeToCallerMatcher, &calleeToCallerHandler);
    discoverGlobalVarsMatchFinder.addMatcher(inputsMatcher, &inputsHandler);
    discoverGlobalVarsMatchFinder.addMatcher(resultsMatcher, &resultsHandler);
    discoverGlobalVarsMatchFinder.addMatcher(stdinMatcher, &stdinHandler);

    rewriteGlobalVarsMatchFinder.addMatcher(globalVarsAsParamsMatcher, &globalVarsAsParamsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(globalVarsAsArgsMatcher, &globalVarsAsArgsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(inputsAndResultsAsParamsMatcher, &inputsAndResultsAsParamsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(inputsAndResultsAsArgsMatcher, &inputsAndResultsAsArgsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(stdinAsParamsMatcher, &stdinAsParamsHandler);
    rewriteGlobalVarsMatchFinder.addMatcher(stdinAsArgsMatcher, &stdinAsArgsHandler);

    mainMatchFinder.addMatcher(functionToTestMatcher, &functionToTestHandler);
    mainMatchFinder.addMatcher(mainMatcher, &mainHandler);
    mainMatchFinder.addMatcher(returnInMainMatcher, &returnInMainHandler);

    includesMatchFinder.addMatcher(includesMatcher, &includesHandler);
  }

  void HandleTranslationUnit(ASTContext &Context) override
  {
    argvMatchFinder.matchAST(Context);
    ioMatchFinder.matchAST(Context);
    discoverGlobalVarsMatchFinder.matchAST(Context);

    buildCallGraph();
    findAllFunctionsWhichUseSpecialVars();

    rewriteGlobalVarsMatchFinder.matchAST(Context);
    mainMatchFinder.matchAST(Context);
    includesMatchFinder.matchAST(Context);
  }
};

/* Frontend Action */
class KernelGenClassAction : public clang::ASTFrontendAction 
{
private:
  Rewriter rewriter;

public: 

  // Write results in a new file
  // NB: A new FrontEndAction will be created for each source file
  void EndSourceFileAction() override 
  {
    const RewriteBuffer *buffer = rewriter.getRewriteBufferFor(rewriter.getSourceMgr().getMainFileID());
    if(buffer == NULL)
    {
      llvm::outs() << "Rewrite buffer is null. Cannot write in file. \n";
      return;
    }

    //include for 'structs.h' and special headers
    string rewriteBuffer = std::string(buffer->begin(), buffer->end());
    string source = "#include \"";
    source.append(STRUCTS_FILENAME);
    source.append("\"\n");
    for(auto inc = includesToAdd.begin(); inc != includesToAdd.end(); inc++)
    {
      source.append("#include \"");
      source.append(*inc);
      source.append("\"\n");
    }

    //comment out includes and typedef bool
    string line;
    istringstream bufferStream(rewriteBuffer);
    while(getline(bufferStream, line))
    {
      istringstream iss(line);
      string token1, token2;
      iss >> token1;
      iss >> token2;

      //includes (only for system headers)
      if(token1.find("#include") != string::npos || token2.find("include") != string::npos)
      {
        if(line.find("<") != string::npos) 
          source.append("//");
      }

      //typedef bool
      if(token1.find("typedef") != string::npos)
      {
        string token3;
        iss >> token3;
        if(token3.find("bool") != string::npos)
          source.append("//");
      }

      source.append(line);
      source.append("\n");
    }

    ofstream clFile;
    clFile.open(testClFilename);
    clFile << source;
    clFile.close();
  }

  virtual unique_ptr<ASTConsumer> CreateASTConsumer(
      CompilerInstance &Compiler, StringRef InFile) override
  {
    rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    return llvm::make_unique<KernelGenClassConsumer>(rewriter);
  }
};

void generateKernel(
    ClangTool * _tool,
    string outputDirectory,
    map<int, string> _argvIdxToInput,
    list<string> _stdinInputs,
    list<struct declaration> _results,
    struct TestFunction _functionToTest)
{
  llvm::outs() << "Generating kernel code... ";
  
  //set global scope variables
  testClFilename = outputDirectory + "/" + "test.cl";
  functionToTest = _functionToTest;
  argvIdxToInput = _argvIdxToInput;
  stdinInputs = _stdinInputs;
  results = _results;

  //generate the kernel code
  Rewriter rewriter;
  _tool->run(newFrontendActionFactory<KernelGenClassAction>().get());

  llvm::outs() << "DONE!\n";
  llvm::outs() << "Finished!\n";
}
