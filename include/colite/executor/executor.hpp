#pragma once

/**
 * @file
 * @brief Executor concepts, invocables and some helpers.
 *
 * The `executor` concept is taken from http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r13.html
 *
 * ## execute(exec, fn)
 *
 * This function object invokes a callable on the provided executor, either via the `exec.execute` member function, or
 * via ADL.
 *
 * ## immediate_executor
 *
 * A minimal executor that simply calls the provided callable immediately.
 *
 * ## any_executor
 *
 * A type-erase helper for executors. Can hold any executor that satisfies the `executor' concept.
 *
 * ## adapt
 *
 * `adapt(adaptable auto)` is a helper function that takes a copy and movable invocable which must be callable with
 * `std::function<void()>` and forwards this callable to the real executor.
 *
 * ### Example
 * ```
 * folly::ManualExecutor folly_exec;
 * auto exec = colite::executor::adapt([&folly_exec](auto fn) {
 *     folly_exec.add(std::move(fn));
 * });
 *
 * colite::executor::execute(exec, [] {
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
     * the executor concept.
     * @tparam E
     */
    template<class E>
    concept executor = detail::executor_of<E, detail::invocable_archetype>;

    /**
     * @brief Concept for a callable that can be adapted to an executor.
     *
     * Any callable that satisfies `std::is_invocable_v<F, std::function<void()>&>` and is both move and copy-contructibe
     * can be adapted to become an executor.
     *
     * @tparam F The function type that the concept is checked for.
     */
    template<class F>
    concept adaptable = std::invocable<std::remove_cvref_t<F>, std::function<void()> &> &&
                            std::move_constructible<std::remove_cvref_t<F>> &&
                            std::copy_constructible<std::remove_cvref_t<F>>;

    namespace detail
    {
        struct not_executor {};

        static_assert(!executor<not_executor>);

        struct adl_executor {
            friend bool operator==(const adl_executor&, const adl_executor&) noexcept {
                return true;
            }
        };
        template<class F>
        void execute(adl_executor, F&&) {}
        static_assert(executor<adl_executor>, "ADL executor!");

        struct member_executor {
            friend bool operator==(const member_executor&, const member_executor&) noexcept {
                return true;
            }

            template<class F>
            void execute(F&&) const {

            }
        };
        static_assert(executor<member_executor>, "Member executor");

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
     * @brief A function object that is used to execute functions on a specified executor.
     *
     * This is used as `colite::executor::execute(some_executor, some_function)` where `some_executor` satisfies `executor` and
     * `some_function` satisfies `std::invocable`.
     *
     * This function object does not participate in ADL.
     */
    inline constexpr detail::execute_t execute;

    /**
     * @brief A simple immediate-executor that simply calls the supplied function immediately.
     */
    struct immediate_executor {
        friend bool operator==(const immediate_executor&, const immediate_executor&) noexcept {
            return true;
        }

        template<std::invocable Func>
        void execute(Func&& f) const {
            std::invoke(std::forward<Func>(f));
        }
    };

    static_assert(executor<immediate_executor>, "immediate executor");

    /**
     * @brief Provides a type-erased wrapper for executors.
     *
     * `any_executor` satisfies `executor` concept and can hold any other type of executor.
     */
    class any_executor {
        struct executor_base {
            virtual ~executor_base() = default;
            virtual void invoke(std::function<void()> func) const = 0;
            virtual std::unique_ptr<executor_base> clone() const noexcept = 0;
        };
        template<executor Exec>
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

        template<executor Exec>
        static std::unique_ptr<executor_base> make_impl(Exec&& exec) {
            using type_erased_t = type_erased_executor<std::remove_cvref_t<Exec>>;
            return std::make_unique<type_erased_t>(std::forward<Exec>(exec));
        }

        std::unique_ptr<executor_base> impl_;
    public:
        template<class Exec> requires (!std::is_same_v<std::remove_cvref_t<Exec>, any_executor> && executor<Exec>)
        any_executor(Exec&& exec): impl_(make_impl(std::forward<Exec>(exec))) {}

        any_executor(const any_executor& rhs) noexcept: impl_(rhs.impl_->clone()) {}
        any_executor(any_executor&&) noexcept = default;

        any_executor& operator=(const any_executor& rhs) {
            using std::swap;
            any_executor temp(rhs);
            swap(impl_, temp.impl_);
            return *this;
        }
        any_executor& operator=(any_executor&&) noexcept = default;

        friend bool operator==(const any_executor& lhs, const any_executor& rhs) noexcept {
            return lhs.impl_.get() == rhs.impl_.get();
        }

        template<std::invocable Func>
        void execute(Func&& f) const {
            impl_->invoke({std::forward<Func>(f)});
        }
    };

    static_assert(executor<any_executor>, "any executor");

    /**
     * @brief Adapt an invocable object to be an executor. The invocable object must be invocable with `std::function<void()>` arguments
     * @tparam Fn The object to adapt to an executor
     *
     * This allows users to create executors from lambdas for instance.
     *
     * The invocable must be copyable.
     *
     */
    template<adaptable Fn>
    auto adapt(Fn&& fn) {
        using fn_t = std::remove_cvref_t<Fn>;
        static_assert(executor<detail::adapted_exec_t<fn_t>>, "Callable cannot be adapted to an executor. Maybe it is not copyable?");
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
        static_assert(e::executor<decltype(e::adapt(static_test_fn))>, "adapt");
        static_assert(e::executor<decltype(e::adapt(static_test_fn_const_ref))>, "adapt");
    }
}