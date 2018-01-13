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

#ifndef CPU_CODE_GENERATOR_H
#define CPU_CODE_GENERATOR_H

#include "Utils.h"
#include <fstream>

void generateCompareResults(std::ofstream &,
                            const std::list<struct ResultDeclaration> &);
void generateHeaderFile();
void generatePopulateInputs(std::ofstream &,
                            const std::list<struct Declaration> &,
                            const std::list<struct Declaration> &);
void generateSourceFile();

#endif
