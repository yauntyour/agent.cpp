// std::move_only_function, for standard libraries that lack it ----------------
// -----------------------------------------------------------------------------
// A minimal stand-in for C++23's std::move_only_function (P0288R9), living in
// cell::compat; the functional/ header next to this file aliases it into
// namespace std when the real one is missing — no libc++ has shipped the
// paper (checked up to LLVM 23), while libstdc++ has it since GCC 12 and MSVC
// since 19.32.
//
// It is a separate header — rather than code embedded in functional/, the way
// generator/ carries its own implementation — so the polyfill itself can be
// compiled and exercised against toolchains whose standard library already
// provides the real type.
//
// Requires <functional> to have been included (std::invoke); functional/
// guarantees that on the path cell.cpp takes.
//
// Supported (the subset cell.cpp relies on):
//   * default and nullptr_t construction, `if (f)` / `!f` emptiness tests,
//   * construction and assignment from any callable invocable with the
//     signature — move-only lambdas, the reason the type exists, included,
//   * move construction/assignment and invocation through operator().
// Not supported; extend this file if cell.cpp starts needing them:
//   * signatures with noexcept or ref-qualifiers (the primary template is
//     deliberately undefined),
//   * a const operator() (targets are invoked non-const; cell.cpp only calls
//     live, non-const wrappers),
//   * small-buffer optimisation — every target is boxed on the heap.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace cell
{
    namespace compat
    {
        template <class Signature>
        class move_only_function;

        template <class R, class... Args>
        class move_only_function<R(Args...)>
        {
            // one virtual per stored callable: the type-erased invocation.
            // holder owns the callable itself, so move-only captures survive.
            struct holder_base
            {
                virtual ~holder_base() = default;
                virtual R call(Args... args) = 0;
            };
            template <class F>
            struct holder final : holder_base
            {
                F fn;
                template <class U>
                explicit holder(U &&f) : fn(std::forward<U>(f)) {}
                R call(Args... args) override
                {
                    if constexpr (std::is_void_v<R>)
                        std::invoke(fn, std::forward<Args>(args)...);
                    else
                        return std::invoke(fn, std::forward<Args>(args)...);
                }
            };

            std::unique_ptr<holder_base> target_;

        public:
            move_only_function() noexcept = default;
            move_only_function(std::nullptr_t) noexcept {}

            // callable -> wrapper. The constraint spells `T &`: a stored
            // callable is invoked as an lvalue, which is what keeps mutable
            // lambdas usable
            template <class F, class T = std::remove_cvref_t<F>>
                requires(!std::is_same_v<T, move_only_function> &&
                         std::is_invocable_r_v<R, T &, Args...>)
            move_only_function(F &&f)
                : target_(std::make_unique<holder<T>>(std::forward<F>(f))) {}

            move_only_function(move_only_function &&) noexcept = default;
            move_only_function &operator=(move_only_function &&) noexcept = default;
            move_only_function(const move_only_function &) = delete;
            move_only_function &operator=(const move_only_function &) = delete;
            ~move_only_function() = default;

            template <class F, class T = std::remove_cvref_t<F>>
                requires(!std::is_same_v<T, move_only_function> &&
                         std::is_invocable_r_v<R, T &, Args...>)
            move_only_function &operator=(F &&f)
            {
                target_ = std::make_unique<holder<T>>(std::forward<F>(f));
                return *this;
            }
            move_only_function &operator=(std::nullptr_t) noexcept
            {
                target_.reset();
                return *this;
            }

            explicit operator bool() const noexcept { return target_ != nullptr; }

            R operator()(Args... args)
            {
                return target_->call(std::forward<Args>(args)...);
            }
        };
    } // namespace compat
} // namespace cell
