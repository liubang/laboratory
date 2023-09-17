//=====================================================================
//
// traits_test.h -
//
// Created by liubang on 2023/06/11 20:21
// Last Modified: 2023/06/11 20:21
//
//=====================================================================
#pragma once

#include <cmath>
#include <limits>
#include <type_traits>

#include "cpp/meta/traits.h"

namespace pl::ut {

template <typename T, enable_if_t<std::is_integral_v<T>>* = nullptr>
constexpr bool numEq(T lhs, T rhs) {
  return lhs == rhs;
}

template <typename T, enable_if_t<is_floating_point_v<T>>* = nullptr>
bool numEq(T lhs, T rhs) {
  return ::fabs(lhs - rhs) < std::numeric_limits<T>::epsilon();
}

template <typename T>
bool numEqImpl(T lhs, T rhs, std::true_type) {
  return ::fabs(lhs - rhs) < std::numeric_limits<T>::epsilon();
}

template <typename T>
bool numEqImpl(T lhs, T rhs, std::false_type) {
  return lhs == rhs;
}

template <typename T>
auto numEqNew(T lhs, T rhs) -> enable_if_t<std::is_arithmetic_v<T>, bool> {
  return numEqImpl(lhs, rhs, is_floating_point<T>{});
};

}  // namespace pl::ut