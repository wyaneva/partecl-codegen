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

#ifndef UTILS_H
#define UTILS_H

#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <string>

bool contains(const std::string &, const std::string &);
std::string read_file(std::string filename);
std::string getDeclAsString(clang::Decl *, clang::Rewriter);
std::string getStmtAsString(clang::Stmt *, clang::Rewriter);

enum class TestedValueType { functionCall, variable };

static struct TestedValue {
  std::string name;
  TestedValueType type;
  int resultArg; // set to -1 if the user is interested in the return result or
                 // type is variable
} TestedValue;

static struct Declaration {
  std::string type;
  std::string name;
  bool isArray;
  bool isConst;
  bool isPointer;
  std::string size; // set to empty when not array
} Declaration;

static struct ResultDeclaration {
  struct Declaration declaration;
  struct TestedValue testedValue;
} ResultValue;

static std::map<std::string, std::string> functionToHeaderFile = {
    {"isalnum", "cl-ctype.h"},  {"isalpha", "cl-ctype.h"},
    {"isascii", "cl-ctype.h"},  {"iscntrl", "cl-ctype.h"},
    {"isdigit", "cl-ctype.h"},  {"isgraph", "cl-ctype.h"},
    {"islower", "cl-ctype.h"},  {"isprint", "cl-ctype.h"},
    {"ispunct", "cl-ctype.h"},  {"isspace", "cl-ctype.h"},
    {"isupper", "cl-ctype.h"},  {"isxdigit", "cl-ctype.h"},
    {"isxlower", "cl-ctype.h"}, {"isxupper", "cl-ctype.h"},
    {"toascii", "cl-ctype.h"},  {"tolower", "cl-ctype.h"},
    {"toupper", "cl-ctype.h"},  {"atoi", "cl-stdlib.h"},
    {"atol", "cl-stdlib.h"},      {"fgets", "cl-stdio.h"},
    {"fgetc", "cl-stdio.h"},    {"strcmp", "cl-string.h"},
    {"strlen", "cl-string.h"}};

#endif
