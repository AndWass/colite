#pragma once

/**
 * @file
 * @brief Executor concepts, invocables and some helpers.
 *
 * The `Executor` concept is taken from http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r13.html
 *
 * ## execute(exec, fn)
 *
 * This function object invokes a callable on the provided Executor, either via the `exec.execute` member function, or
 * via ADL.
 *
 * ## ImmediateExecutor
 *
 * A minimal Executor that simply calls the provided callable immediately.
 *
 * ## AnyExecutor
 *
 * A type-erase helper for executors. Can hold any Executor that satisfies the `Executor' concept.
 *
 * ## adapt
 *
 * `adapt(Adaptable auto)` is a helper function that takes a copy and movable invocable which must be callable with
 * `std::function<void()>` and forwards this callable to the real Executor.
 *
 * ### Example
 * ```
 * folly::ManualExecutor folly_exec;
 * auto exec = colite::Executor::adapt([&folly_exec](auto fn) {
 *     folly_exec.add(std::move(fn));
 * });
 *
 * colite::Executor::execute(exec, [] {
 *     std::cout << "Hello world" << std::endl;
 * });
 *
 * folly_exec.run(); // Prints "Hello world"
 * ```
 */

#include <memory>
#include <type_traits>
#include <concepts>
#include <functional>

namespace colite::executor
{

    namespace detail {
        void execute();

        template<class E, class F>
        concept member_execute = requires(const E &e, F &&f) {
            e.execute((F&&)f);
        };

        template<class E, class F>
        concept adl_execute = requires(const E &e, F &&f) {
            execute(e, (F&&)f);
        };

        struct execute_t
        {
            template<class E, class F> requires member_execute<E, F>
            constexpr void operator()(E&& e, F&& f) const
            {
                ((E&&)e).execute((F&&)f);
            }

            template<class E, class F> requires (!member_execute<E, F> && adl_execute<E, F>)
            constexpr void operator()(E&& e, F&& f) const
            {
                execute((E&&)e, (F&&)f);
            }
        };

        template<class E, class F>
        concept executor_of = std::invocable<std::remove_cvref_t<F> &> &&
                              std::constructible_from<std::remove_cvref_t<F>, F> &&
                              std::move_constructible<std::remove_cvref_t<F>> &&
                              std::copy_constructible<E> &&
                              std::is_nothrow_copy_constructible_v<E> &&
                              std::equality_comparable<E> &&
                              requires(const E& e, F&& f) {
                                  execute_t()(e, (F&&)f);
                              };

        struct invocable_archetype
        {
            void operator()() noexcept {}
        };

        static_assert(std::invocable<invocable_archetype&>, "bug!");
    }

    /**
     * @brief Executor concept.
     *
     * See http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r13.html for the general gist of
     * the Executor concept.
     * @tparam E
     */
    template<class E>
    concept Executor = detail::executor_of<E, detail::invocable_archetype>;

    /**
     * @brief Concept for a callable that can be adapted to an Executor.
     *
     * Any callable that satisfies `std::is_invocable_v<F, std::function<void()>&>` and is both move and copy-contructibe
     * can be adapted to become an Executor.
     *
     * @tparam F The function type that the concept is checked for.
     */
    template<class F>
    concept Adaptable = std::invocable<std::remove_cvref_t<F>, std::function<void()> &> &&
                            std::move_constructible<std::remove_cvref_t<F>> &&
                            std::copy_constructible<std::remove_cvref_t<F>>;

    namespace detail
    {
        struct not_executor {};

        static_assert(!Executor<not_executor>);

        struct adl_executor {
            friend bool operator==(const adl_executor&, const adl_executor&) noexcept {
                return true;
            }
        };
        template<class F>
        void execute(adl_executor, F&&) {}
        static_assert(Executor<adl_executor>, "ADL Executor!");

        struct member_executor {
            friend bool operator==(const member_executor&, const member_executor&) noexcept {
                return true;
            }

            template<class F>
            void execute(F&&) const {

            }
        };
        static_assert(Executor<member_executor>, "Member Executor");

        template<std::invocable<std::function<void()>> E>
        struct adapted_exec_t
        {
            E fn_;

            friend bool operator==(const adapted_exec_t&, const adapted_exec_t&) {
                return false;
            }

            template<std::invocable F>
            void execute(F&& f) const {
                fn_(std::forward<F>(f));
            }
        };
    }

    /**
     * @brief A function object that is used to execute functions on a specified Executor.
     *
     * This is used as `colite::Executor::execute(some_executor, some_function)` where `some_executor` satisfies `Executor` and
     * `some_function` satisfies `std::invocable`.
     *
     * This function object does not participate in ADL.
     */
    inline constexpr detail::execute_t execute;

    /**
     * @brief A simple immediate-Executor that simply calls the supplied function immediately.
     */
    struct ImmediateExecutor {
        friend bool operator==(const ImmediateExecutor &, const ImmediateExecutor &) noexcept {
            return true;
        }

        template<std::invocable Func>
        void execute(Func&& f) const {
            std::invoke(std::forward<Func>(f));
        }
    };

    static_assert(Executor<ImmediateExecutor>, "immediate Executor");

    /**
     * @brief Provides a type-erased wrapper for executors.
     *
     * `AnyExecutor` satisfies `Executor` concept and can hold any other type of Executor.
     */
    class AnyExecutor {
        struct executor_base {
            virtual ~executor_base() = default;
            virtual void invoke(std::function<void()> func) const = 0;
            virtual std::unique_ptr<executor_base> clone() const noexcept = 0;
        };
        template<Executor Exec>
        struct type_erased_executor final: executor_base {
            Exec exec_;

            type_erased_executor(const Exec& exec): exec_(exec) {}
            type_erased_executor(Exec&& exec): exec_(std::move(exec)) {}

            void invoke(std::function<void()> func) const override {
                ::colite::executor::execute(exec_, std::move(func));
            }
            std::unique_ptr<executor_base> clone() const noexcept override {
                return std::make_unique<type_erased_executor<Exec>>(exec_);
            }
        };

        template<Executor Exec>
        static std::unique_ptr<executor_base> make_impl(Exec&& exec) {
            using type_erased_t = type_erased_executor<std::remove_cvref_t<Exec>>;
            return std::make_unique<type_erased_t>(std::forward<Exec>(exec));
        }

        std::unique_ptr<executor_base> impl_;
    public:
        template<class Exec> requires (!std::is_same_v<std::remove_cvref_t<Exec>, AnyExecutor> && Executor<Exec>)
            AnyExecutor(Exec&& exec): impl_(make_impl(std::forward<Exec>(exec))) {}

        AnyExecutor(const AnyExecutor & rhs) noexcept: impl_(rhs.impl_->clone()) {}
        AnyExecutor(AnyExecutor &&) noexcept = default;

        AnyExecutor & operator=(const AnyExecutor & rhs) {
            using std::swap;
            AnyExecutor temp(rhs);
            swap(impl_, temp.impl_);
            return *this;
        }
        AnyExecutor & operator=(AnyExecutor &&) noexcept = default;

        friend bool operator==(const AnyExecutor & lhs, const AnyExecutor & rhs) noexcept {
            return lhs.impl_.get() == rhs.impl_.get();
        }

        template<std::invocable Func>
        void execute(Func&& f) const {
            impl_->invoke({std::forward<Func>(f)});
        }
    };

    static_assert(Executor<AnyExecutor>, "any Executor");

    /**
     * @brief Adapt an invocable object to be an Executor. The invocable object must be invocable with `std::function<void()>` arguments
     * @tparam Fn The object to adapt to an Executor
     *
     * This allows users to create executors from lambdas for instance.
     *
     * The invocable must be copyable.
     *
     */
    template<Adaptable Fn>
    auto adapt(Fn&& fn) {
        using fn_t = std::remove_cvref_t<Fn>;
        static_assert(Executor<detail::adapted_exec_t<fn_t>>, "Callable cannot be adapted to an Executor. Maybe it is not copyable?");
        return detail::adapted_exec_t<fn_t>{
            std::forward<Fn>(fn)
        };
    }

    template<class R, class A> requires std::invocable<A> && std::constructible_from<std::function<void()>, A>
    auto adapt(R (*fn)(A)) {
        return detail::adapted_exec_t<R(*)(A)> {
            fn
        };
    }

    namespace detail
    {
        namespace e = ::colite::executor;
        void static_test_fn(std::function<void()> fn);
        void static_test_fn_const_ref(const std::function<void()>& fn);
        static_assert(e::Executor<decltype(e::adapt(static_test_fn))>, "adapt");
        static_assert(e::Executor<decltype(e::adapt(static_test_fn_const_ref))>, "adapt");
    }
}