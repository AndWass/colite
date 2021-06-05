#pragma once

/**
 * @file
 * @brief Yield function to yield to an executor.
 */

#include <coroutine>

#include <colite/executor/executor.hpp>

namespace colite::coroutine
{
    /**
     * @brief Provide an awaitable that yields once to the provided executor.
     * @param exec The executor to resume the coroutine on again.
     * @return An awaitable that yields once.
     */
    template<colite::executor::executor Exec>
    [[nodiscard]] auto yield(Exec&& exec) {
        using exec_t = std::remove_cvref_t<Exec>;

        struct awaitable
        {
            exec_t exec_;
            static constexpr bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> to_suspend) {
                colite::executor::execute(exec_, [to_suspend] {
                    to_suspend.resume();
                });
            }

            void await_resume() {}
        };

        return awaitable{std::forward<Exec>(exec)};
    }
}