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

#include<string>
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#define STRUCTS_FILENAME "structs.h"

using namespace std;
using namespace clang;

string read_file(string filename);
string getDeclAsString(Decl *, Rewriter);
string getStmtAsString(Stmt *, Rewriter);

enum class TestedValueType 
{ 
  functionCall, 
  variable 
};

static struct TestedValue
{
  TestedValueType type;
  string name;
  int resultArg; // set to -1 if the user is interested in the return result or type is variable
} TestedValue;

static struct declaration
{
  string type;
  string name;
  bool isArray;
  bool isPointer;
  int length; // set to -1 if not array
} declaration;

static map<string, string> functionToHeaderFile = 
{
  {"isalnum", "cl-ctype.h"},
  {"isalpha", "cl-ctype.h"},
  {"isascii", "cl-ctype.h"},
  {"iscntrl", "cl-ctype.h"},
  {"isdigit", "cl-ctype.h"},
  {"isgraph", "cl-ctype.h"},
  {"islower", "cl-ctype.h"},
  {"isprint", "cl-ctype.h"},
  {"ispunct", "cl-ctype.h"},
  {"isspace", "cl-ctype.h"},
  {"isupper", "cl-ctype.h"},
  {"isxdigit", "cl-ctype.h"},
  {"isxlower", "cl-ctype.h"},
  {"isxupper", "cl-ctype.h"},
  {"toascii", "cl-ctype.h"},
  {"tolower", "cl-ctype.h"},
  {"toupper", "cl-ctype.h"},
  {"atoi", "cl-atoi.h"},
  {"fgets", "cl-fgets.h"},
};

#endif
