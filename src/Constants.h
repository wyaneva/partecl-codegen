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

//constants used in the configuration file
namespace config_constants 
{
  static const char* FUNCTION = "function:";
  static const char* VARIABLE = "variable:";
  static const char* RET = "RET";
  static const char* ARG = "ARG";
  static const char* INPUT = "input:";
  static const char* STDIN = "stdin:";
  static const char* RESULT = "result:";
}

//filenames
namespace filename_constants
{
  static const char* STRUCTS_FILENAME = "structs.h";
  static const char* CPU_GEN_FILENAME = "cpu-gen";
}

//status
namespace status_constants
{
  static const int FAIL = 0;
  static const int SUCCESS = 1;
}

namespace structs_constants
{
  static const char* ARGC = "argc";
  static const char* INPUT_S = "input";
  static const char* RESULT_S = "result";
  static const char* TEST_CASE_NUM = "test_case_num";
}

#endif 

