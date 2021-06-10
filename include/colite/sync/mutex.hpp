#pragma once

/**
 * @file
 * @brief Async mutex for sharing resources between tasks
 *
 * A mutex provides synchronization of values between tasks. Unlike `std::mutex` this `mutex`
 * holds the guarded value within the mutex. Locking the mutex returns a guard that can be used to access the value
 * and unlocking the mutex again. A guard is automatically unlocked when destroyed.
 *
 * For instance to provide mutually exclusive access to a string one would use a `colite::sync::mutex<std::string> mutex`
 *
 * ## Example
 *
 * ```cpp
 * task my_task(colite::sync::mutex<std::string>& string_value) {
 *     colite::sync::mutex_guard<std::string> value = co_await string_value.lock(my_exec);
 *     // Once control reaches here this task has exclusive access to value
 *     *value = "Hello world"; // Update the value
 *     // value.unlock(); Not necessary, done automatically at the end of scope.
 * }
 * ```
 */

#include <vector>
#include <coroutine>
#include <mutex>
#include <memory>
#include <optional>

#include <colite/executor/executor.hpp>

namespace colite::sync
{
    template<class T>
    class mutex;

    /**
     * @brief A mutex guard that automatically unlocks the mutex on destruction
     * @tparam T The value type held by the mutex
     *
     * This is the return type from `co_await some_mutex.lock(my_exec)`
     *
     * A guard is not copy constructible/assignable. It is however movable.
     */
    template<class T>
    class mutex_guard
    {
        template<class>
        friend class mutex;

        mutex<T>* mutex_ = nullptr;

        mutex_guard(mutex<T>& mutex_): mutex_(&mutex_) {}

        void unlock_impl();
    public:
        mutex_guard() = default;
        mutex_guard(const mutex_guard&) = delete;
        mutex_guard(mutex_guard&& rhs) noexcept: mutex_(std::exchange(rhs.mutex_, nullptr)) {}

        mutex_guard& operator=(const mutex_guard&) = delete;
        mutex_guard& operator=(mutex_guard&& rhs) noexcept;

        ~mutex_guard();

        /**
         * @brief Unlock the mutex before destruction.
         *
         * It is safe to unlock and already unlocked mutex, this will simply do nothing.
         */
        void unlock() {
            unlock_impl();
        }

        T& operator*() noexcept;
        const T& operator*() const noexcept;

        T* operator->() noexcept;
        const T* operator->() const noexcept;
    };

    template<class T>
    class mutex
    {
        template<class>
        friend class mutex_guard;

        struct waiter_t
        {
            mutex* mutex_;
            std::coroutine_handle<> coroutine_;
            executor::any_executor exec_;

            waiter_t(mutex* m, executor::executor auto exec): mutex_(m), exec_(std::move(exec)) {}
        };

        std::mutex mut_;
        bool locked_ = false;
        T value_;
        std::vector<std::weak_ptr<waiter_t>> waiters_;

        void wakeup_waiter(std::shared_ptr<waiter_t> waiter) {
            std::weak_ptr<waiter_t> weak_waiter = waiter;
            // Handler is posted to the waiters executor, where a life-time check of the associated awaitable is done
            //
            // Then a lock attempt is made, if unsuccessful then the associated waiter is added to the list
            // of waiters.
            auto handler = [weak_waiter] {
              if(auto waiter = weak_waiter.lock()) {
                  std::unique_lock lock(waiter->mutex_->mut_);
                  if(!waiter->mutex_->locked_) {
                      waiter->mutex_->locked_ = true;
                      lock.unlock();
                      waiter->coroutine_.resume();
                  }
                  else {
                      waiter->mutex_->waiters_.push_back(waiter);
                  }
              }
            };
            auto exec = waiter->exec_;
            waiter.reset();
            executor::execute(std::move(exec), handler);
        }
        void wakeup_waiters() {
            std::vector<std::weak_ptr<waiter_t>> waiters = [&] {
              std::lock_guard lock{mut_};
              return std::exchange(waiters_, std::vector<std::weak_ptr<waiter_t>>{});
            }();
            // Wakeup all waiters to "poll" on the mutex. Let them race to attempt to lock it again
            // Any unsuccessful lock-attempts will cause the waiters to be re-added to the list of
            // active waiters.
            for(auto weak_waiter: waiters) {
                if(auto waiter = weak_waiter.lock()) {
                    wakeup_waiter(std::move(waiter));
                }
            }
        }
    public:
        explicit mutex(T value): value_(std::move(value)) {}

        /**
         * @brief Attempt to lock the mutex.
         * @return An optional mutex_guard.
         *
         * This attempts to lock the mutex in a synchronous non-blocking manner.
         *
         * Returns an empty optional if lock was unsuccessful, otherwise it holds a mutex_guard<T>.
         */
        std::optional<mutex_guard<T>> try_lock() & noexcept {
            std::lock_guard lock(mut_);
            if(!locked_) {
                locked_ = true;
                return mutex_guard<T>(*this);
            }
            return std::nullopt;
        }

        /**
         * @brief Asynchronously lock the mutex.
         * @param exec The executor associated with the coroutine
         * @return An awaitable object.
         *
         * When `co_await mutex.lock(exec)` has finished, the mutex is locked.
         * `co_await mutex.lock(exec)` will produce a `mutex_guard<T>` that can
         * be used to read and modify the value associated with the mutex.
         */
        auto lock(colite::executor::executor auto exec) & {
            struct awaitable {
                std::shared_ptr<waiter_t> waiter_;
                static bool await_ready() noexcept {
                    return false;
                }

                bool await_suspend(std::coroutine_handle<> to_suspend) {
                    std::lock_guard lock(waiter_->mutex_->mut_);
                    waiter_->coroutine_ = to_suspend;
                    if(!waiter_->mutex_->locked_) {
                        waiter_->mutex_->locked_ = true;
                        return false;
                    }
                    waiter_->mutex_->waiters_.push_back(waiter_);
                    return true;
                }

                mutex_guard<T> await_resume() {
                    return {*waiter_->mutex_};
                }
            };

            return awaitable{std::make_shared<waiter_t>(this, std::move(exec))};
        }
    };

    template<class T>
    mutex_guard<T>::~mutex_guard() {
        unlock_impl();
    }
    template<class T>
    T &mutex_guard<T>::operator*() noexcept {
        return mutex_->value_;
    }
    template<class T>
    const T &mutex_guard<T>::operator*() const noexcept {
        return mutex_->value_;
    }
    template<class T>
    T *mutex_guard<T>::operator->() noexcept {
        return &mutex_->value_;
    }
    template<class T>
    const T *mutex_guard<T>::operator->() const noexcept {
        return &mutex_->value_;
    }
    template<class T>
    void mutex_guard<T>::unlock_impl() {
        if(mutex_) {
            {
                std::lock_guard lock(mutex_->mut_);
                mutex_->locked_ = false;
            }
            mutex_->wakeup_waiters();
            mutex_ = nullptr;
        }
    }
    template<class T>
    mutex_guard<T> &mutex_guard<T>::operator=(mutex_guard &&rhs) noexcept {
        if(this != &rhs) {
            unlock_impl();
            mutex_ = std::exchange(rhs.mutex_, nullptr);
        }
        return *this;
    }
}