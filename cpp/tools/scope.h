//=====================================================================
//
// scope.h -
//
// Created by liubang on 2023/06/08 14:56
// Last Modified: 2023/06/08 14:56
//
//=====================================================================
#pragma once

#include <type_traits>
#include <utility>

#include "cpp/tools/preprocessor.h"

namespace pl {

template <typename Fn> class ScopeGuard {
public:
    ScopeGuard(Fn&& fn) : fn_(std::move(fn)) {}
    ~ScopeGuard() {
        if (enabled_)
            fn_();
    }

    void dismiss() { enabled_ = false; }

private:
    Fn fn_;
    bool enabled_{true};
};

namespace detail {

enum class ScopeGuardOnExit {};
template <typename Fn>
inline ScopeGuard<std::decay_t<Fn>> operator+(detail::ScopeGuardOnExit, Fn&& fn) {
    return ScopeGuard<Fn>(std::forward<Fn>(fn));
}

} // namespace detail

#define SCOPE_EXIT                                                    \
    auto PL_ANONYMOUS_VARIABLE(__playground_cpp_tools_ScopeGuard__) = \
        pl::detail::ScopeGuardOnExit{} + [&]() noexcept

} // namespace pl
