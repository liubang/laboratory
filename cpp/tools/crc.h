//=====================================================================
//
// crc.h -
//
// Created by liubang on 2023/06/01 18:56
// Last Modified: 2023/06/01 18:56
//
//=====================================================================
#pragma once

#include <boost/crc.hpp>
#include <cstddef>

namespace pl::tools {

uint32_t crc32(const void* data, size_t length);

}  // namespace pl::tools
