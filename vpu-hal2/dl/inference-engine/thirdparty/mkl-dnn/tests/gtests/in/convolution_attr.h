/*******************************************************************************
* Copyright 2017 Intel Corporation
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
*******************************************************************************/

INST_TEST_CASE(SimpleSmall_Blocked_Attributes,
    PARAMS_ATTR(nhwc, FMT_WEIGHTS_BLOCKED16, FMT_BIAS, nhwc,
        round_nearest, 0.3f, COMMON,
        2, 1, 32, 13, 13, 32, 12, 12, 3, 3, 0, 0, 1, 1),
    PARAMS_ATTR(nhwc, FMT_WEIGHTS_BLOCKED16, FMT_BIAS, nhwc,
        round_down, 0.3f, COMMON,
        2, 1, 32, 13, 13, 32, 12, 12, 3, 3, 0, 0, 1, 1),
    PARAMS_ATTR(nhwc, FMT_WEIGHTS_BLOCKED16, FMT_BIAS, nhwc,
        round_nearest, 0.5f, COMMON,
        2, 1, 32, 13, 13, 32, 12, 12, 3, 3, 0, 0, 1, 1),
    PARAMS_ATTR(nhwc, FMT_WEIGHTS_BLOCKED16, FMT_BIAS, nhwc,
        round_nearest, 0.5f, COMMON,
        2, 1, 32, 13, 13, 32, 12, 12, 3, 3, 0, 0, 1, 1)
);
