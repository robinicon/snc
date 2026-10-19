// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
//
// config.h — 示例工程统一配置常量（默认端口等）。
//
// WARNING: 生产环境必须通过环境变量 SNC_JWT_SECRET 设置密钥，不得依赖此处任何默认值。
// 此文件仅用于示例演示，不提供默认 JWT secret。

#include <cstdlib>
#include <string>
#include <stdexcept>

namespace snc_examples {

inline std::string JwtSecret() {
    const char* env = std::getenv("SNC_JWT_SECRET");
    if (!env || env[0] == '\0') {
        throw std::runtime_error(
            "SNC_JWT_SECRET environment variable is not set. "
            "Production builds require a ≥32-byte random secret. "
            "Set it before running any example.");
    }
    return std::string(env);
}

inline bool JwtSecretFromEnv() {
    const char* env = std::getenv("SNC_JWT_SECRET");
    return env != nullptr && env[0] != '\0';
}

} // namespace snc_examples
