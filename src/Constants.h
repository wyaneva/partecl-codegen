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

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>

//filenames
namespace filename_constants
{
  const std::string STRUCTS_FILENAME = "structs.h";
  const std::string CPU_GEN_FILENAME = "cpu-gen";
}

//constants used in the configuration file
namespace config_constants 
{
  const std::string FUNCTION = "function:";
  const std::string VARIABLE = "variable:";
  const std::string RET = "RET";
  const std::string ARG = "ARG";
  const std::string INPUT = "input:";
  const std::string STDIN = "stdin:";
  const std::string RESULT = "result:";
}

//status
namespace status_constants
{
  const int FAIL = 0;
  const int SUCCESS = 1;
}

#endif 

