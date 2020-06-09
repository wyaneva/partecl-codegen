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

// constants used in the configuration file
namespace config_constants {
const char *const FUNCTION = "function:";
const char *const VARIABLE = "variable:";
const char *const RET = "RET";
const char *const ARG = "ARG";
const char *const INPUT = "input:";
const char *const STDIN = "stdin:";
const char *const OUTPUT = "output:";
const char *const INCLUDE = "include:";
} // namespace config_constants

// filenames
namespace filename_constants {
const char *const STRUCTS_FILENAME = "structs.h";
const char *const CPU_GEN_FILENAME = "cpu-gen";
} // namespace filename_constants

// status
namespace status_constants {
const int FAIL = 0;
const int SUCCESS = 1;
} // namespace status_constants

namespace structs_constants {
const char *const ARGC = "argc";
const char *const INPUT = "partecl_input";
const char *const OUTPUT = "partecl_output";
const char *const TEST_CASE_NUM = "test_case_num";
const int POINTER_ARRAY_SIZE = 500;
} // namespace structs_constants

#endif
