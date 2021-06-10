#pragma once

#include <colite/executor/executor.hpp>
#include <coroutine>

namespace detail
{
    class task
    {
    public:
        struct promise_type;
    private:
        struct state {
            bool done_ = false;
            std::coroutine_handle<promise_type> my_handle_;
            std::coroutine_handle<> to_resume_on_finish_;
        };
        std::shared_ptr<state> state_;
        task(std::shared_ptr<state> state_): state_(std::move(state_)) {}
    public:
        //task() = default;
        task(const task&) = delete;
        task(task&&) noexcept =  default;
        task& operator=(const task&) = delete;
        task& operator=(task&&) noexcept = default;

        ~task() {
            if(state_ && !is_done()) {
                state_->my_handle_.destroy();
            }
        }
        [[nodiscard]] bool is_done() const {
            return state_ && state_->done_;
        }

        void start_on(colite::executor::Executor auto exec) {
            colite::executor::execute(exec, [c = this->state_] {
                c->my_handle_.resume();
            });
        }

        static bool await_ready() noexcept {
            return false;
        }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> to_suspend) {
            state_->to_resume_on_finish_ = to_suspend;
            return state_->my_handle_;
        }

        void await_resume() {

        }

        struct promise_type
        {
            std::shared_ptr<state> state_;
            promise_type(): state_(std::make_shared<state>()) {}

            task get_return_object() {
                state_->my_handle_ = std::coroutine_handle<promise_type>::from_promise(*this);
                return {state_};
            }
            void return_void() {
                state_->done_ = true;
            }

            std::suspend_always initial_suspend() const noexcept {
                return {};
            }

            struct final_suspender
            {
                std::coroutine_handle<> next_;
                bool await_ready() const noexcept {
                    if(next_) {
                        return false;
                    }
                    return true;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
                    return std::move(next_);
                }

                void await_resume() const noexcept {}
            };

            final_suspender final_suspend() const noexcept {
                return { state_->to_resume_on_finish_ };
            }

            void unhandled_exception() {

            }
        };
    };
}