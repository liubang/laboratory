// Copyright (c) 2023 The Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: liubang (it.liubang@gmail.com)

#include <iostream>
#include <memory>

#include "parser.h"

int main(int argc, char* argv[]) {
    std::string flux = R"(
    import "array"
    import "math"
    import "influxdata/influxdb/sample"

    from(bucket:"telegraf/autogen")
        |> range(start:-1h)
        |> filter(fn:(r) =>
            r._measurement == "cpu" and
            r.cpu == "cpu-total"
        )
        |> aggregateWindow(every: 1m, fn: mean)
    )";
    // std::string flux = R"(
    // from(bucket:"telegraf/autogen")
    //     |> range(start:-1h)
    // )";

    pl::Parser parser(flux);
    auto file = parser.parse_file("");

    std::cout << __FILE_NAME__ << ":" << __LINE__ << " ==> " << file->name << "\n";
    std::cout << __FILE_NAME__ << ":" << __LINE__ << " ==> " << file->imports.size() << "\n";
    std::cout << __FILE_NAME__ << ":" << __LINE__ << " ==> " << file->body.size() << "\n";

    for (const auto& stmt : file->body) {
        std::cout << static_cast<int>(stmt->type) << "\n";
        std::cout << stmt->string() << '\n';
    }

    return 0;
}
