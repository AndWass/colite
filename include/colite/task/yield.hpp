#pragma once

/**
 * @file
 * @brief Yield function to yield to an Executor.
 */

#include <coroutine>

#include <colite/executor/executor.hpp>

namespace colite::task
{
    /**
     * @brief Provide an awaitable that yields once to the provided Executor.
     * @param exec The Executor to resume the coroutine on again.
     * @return An awaitable that yields once.
     */
    template<colite::executor::Executor Exec>
    [[nodiscard]] auto yield(Exec&& exec) {
        using exec_t = std::remove_cvref_t<Exec>;

        struct awaitable
        {
            exec_t exec_;
            std::shared_ptr<void> alive_check_;
            static constexpr bool await_ready() noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> to_suspend) {
                std::weak_ptr<void> weak_alive_check_ = alive_check_;
                colite::executor::execute(exec_, [to_suspend, weak_alive_check_] {
                    if(auto alive = weak_alive_check_.lock()) {
                        to_suspend.resume();
                    }
                });
            }

            void await_resume() {}
        };

        return awaitable{std::forward<Exec>(exec), std::make_shared<char>(0)};
    }
}