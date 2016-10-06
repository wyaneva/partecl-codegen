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

#include<fstream>
#include<iostream>
#include<streambuf>
#include<string>
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"

using namespace std;
using namespace clang;

string read_file(string filename)
{
  ifstream t(filename);
  string str;

  t.seekg(0, ios::end);
  str.reserve(t.tellg());
  t.seekg(0, ios::beg);

  str.assign(istreambuf_iterator<char>(t), istreambuf_iterator<char>());

  return str;
}

string getDeclAsString(Decl *decl, Rewriter rewriter)
{
  SourceRange declRange = decl->getSourceRange();
  string declString;

  int rangeSize = rewriter.getRangeSize(declRange);
  if(rangeSize == -1)
  {
    return "";
  }

  SourceLocation locStart = declRange.getBegin();
  const char *strStart = rewriter.getSourceMgr().getCharacterData(locStart);

  declString.assign(strStart, rangeSize);

  return declString;
}

string getStmtAsString(Stmt *stmt, Rewriter rewriter)
{
  SourceRange range = stmt->getSourceRange();
  string stmtString;

  int rangeSize = rewriter.getRangeSize(range);
  if(rangeSize == -1)
  {
    return "";
  }

  SourceLocation locStart = range.getBegin();
  const char *strStart = rewriter.getSourceMgr().getCharacterData(locStart);

  stmtString.assign(strStart, rangeSize);

  return stmtString;
}


