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

#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>

bool contains(const std::string &str1, const std::string &str2) {
  return str1.find(str2) != std::string::npos;
}

std::string read_file(std::string filename) {
  std::ifstream t(filename);
  std::string str;

  t.seekg(0, std::ios::end);
  str.reserve(t.tellg());
  t.seekg(0, std::ios::beg);

  str.assign(std::istreambuf_iterator<char>(t),
             std::istreambuf_iterator<char>());

  return str;
}

std::string getDeclAsString(clang::Decl *decl, clang::Rewriter rewriter) {
  clang::SourceRange declRange = decl->getSourceRange();
  std::string declString;

  int rangeSize = rewriter.getRangeSize(declRange);
  if (rangeSize == -1) {
    return "";
  }

  clang::SourceLocation locStart = declRange.getBegin();
  const char *strStart = rewriter.getSourceMgr().getCharacterData(locStart);

  declString.assign(strStart, rangeSize);

  return declString;
}

std::string getStmtAsString(clang::Stmt *stmt, clang::Rewriter rewriter) {
  clang::SourceRange range = stmt->getSourceRange();
  std::string stmtString;

  int rangeSize = rewriter.getRangeSize(range);
  if (rangeSize == -1) {
    return "";
  }

  clang::SourceLocation locStart = range.getBegin();
  const char *strStart = rewriter.getSourceMgr().getCharacterData(locStart);

  stmtString.assign(strStart, rangeSize);

  return stmtString;
}
