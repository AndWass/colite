#pragma once

/**
 * @file
 * @brief Async Mutex for sharing resources between tasks
 *
 * A Mutex provides synchronization of values between tasks. Unlike `std::Mutex` this `Mutex`
 * holds the guarded value within the Mutex. Locking the Mutex returns a guard that can be used to access the value
 * and unlocking the Mutex again. A guard is automatically unlocked when destroyed.
 *
 * For instance to provide mutually exclusive access to a string one would use a `colite::sync::Mutex<std::string> Mutex`
 *
 * ## Example
 *
 * ```cpp
 * task my_task(colite::sync::Mutex<std::string>& string_value) {
 *     colite::sync::MutexGuard<std::string> value = co_await string_value.lock(my_exec);
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
    class Mutex;

    /**
     * @brief A Mutex guard that automatically unlocks the Mutex on destruction
     * @tparam T The value type held by the Mutex
     *
     * This is the return type from `co_await some_mutex.lock(my_exec)`
     *
     * A guard is not copy constructible/assignable. It is however movable.
     */
    template<class T>
    class MutexGuard {
        template<class>
        friend class Mutex;

        Mutex<T>* mutex_ = nullptr;

        MutexGuard(Mutex<T>& mutex_): mutex_(&mutex_) {}

        void unlock_impl();
    public:
        MutexGuard() = default;
        MutexGuard(const MutexGuard &) = delete;
        MutexGuard(MutexGuard && rhs) noexcept: mutex_(std::exchange(rhs.mutex_, nullptr)) {}

        MutexGuard & operator=(const MutexGuard &) = delete;
        MutexGuard & operator=(MutexGuard && rhs) noexcept;

        ~MutexGuard();

        /**
         * @brief Unlock the Mutex before destruction.
         *
         * It is safe to unlock and already unlocked Mutex, this will simply do nothing.
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
    class Mutex {
        template<class>
        friend class MutexGuard;

        struct waiter_t
        {
            Mutex * mutex_;
            std::coroutine_handle<> coroutine_;
            executor::AnyExecutor exec_;

            waiter_t(Mutex * m, executor::Executor auto exec): mutex_(m), exec_(std::move(exec)) {}
        };

        std::mutex mut_;
        bool locked_ = false;
        T value_;
        std::vector<std::weak_ptr<waiter_t>> waiters_;

        void wakeup_waiter(std::shared_ptr<waiter_t> waiter) {
            std::weak_ptr<waiter_t> weak_waiter = waiter;
            // Handler is posted to the waiters Executor, where a life-time check of the associated awaitable is done
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
              std::scoped_lock lock{mut_};
              return std::exchange(waiters_, std::vector<std::weak_ptr<waiter_t>>{});
            }();
            // Wakeup all waiters to "poll" on the Mutex. Let them race to attempt to lock it again
            // Any unsuccessful lock-attempts will cause the waiters to be re-added to the list of
            // active waiters.
            for(auto weak_waiter: waiters) {
                if(auto waiter = weak_waiter.lock()) {
                    wakeup_waiter(std::move(waiter));
                }
            }
        }
    public:
        explicit Mutex(T value): value_(std::move(value)) {}

        /**
         * @brief Attempt to lock the Mutex.
         * @return An optional MutexGuard.
         *
         * This attempts to lock the Mutex in a synchronous non-blocking manner.
         *
         * Returns an empty optional if lock was unsuccessful, otherwise it holds a MutexGuard<T>.
         */
        std::optional<MutexGuard<T>> try_lock() & noexcept {
            std::scoped_lock lock(mut_);
            if(!locked_) {
                locked_ = true;
                return MutexGuard<T>(*this);
            }
            return std::nullopt;
        }

        /**
         * @brief Asynchronously lock the Mutex.
         * @param exec The Executor associated with the coroutine
         * @return An awaitable object.
         *
         * When `co_await Mutex.lock(exec)` has finished, the Mutex is locked.
         * `co_await Mutex.lock(exec)` will produce a `MutexGuard<T>` that can
         * be used to read and modify the value associated with the Mutex.
         */
        auto lock(colite::executor::Executor auto exec) & {
            struct awaitable {
                std::shared_ptr<waiter_t> waiter_;
                static bool await_ready() noexcept {
                    return false;
                }

                bool await_suspend(std::coroutine_handle<> to_suspend) {
                    std::scoped_lock lock(waiter_->mutex_->mut_);
                    waiter_->coroutine_ = to_suspend;
                    if(!waiter_->mutex_->locked_) {
                        waiter_->mutex_->locked_ = true;
                        return false;
                    }
                    waiter_->mutex_->waiters_.push_back(waiter_);
                    return true;
                }

                MutexGuard<T> await_resume() {
                    return {*waiter_->mutex_};
                }
            };

            return awaitable{std::make_shared<waiter_t>(this, std::move(exec))};
        }
    };

    template<class T>
    MutexGuard<T>::~MutexGuard() {
        unlock_impl();
    }
    template<class T>
    T &MutexGuard<T>::operator*() noexcept {
        return mutex_->value_;
    }
    template<class T>
    const T &MutexGuard<T>::operator*() const noexcept {
        return mutex_->value_;
    }
    template<class T>
    T *MutexGuard<T>::operator->() noexcept {
        return &mutex_->value_;
    }
    template<class T>
    const T *MutexGuard<T>::operator->() const noexcept {
        return &mutex_->value_;
    }
    template<class T>
    void MutexGuard<T>::unlock_impl() {
        if(mutex_) {
            {
                std::scoped_lock lock(mutex_->mut_);
                mutex_->locked_ = false;
            }
            mutex_->wakeup_waiters();
            mutex_ = nullptr;
        }
    }
    template<class T>
    MutexGuard<T> &MutexGuard<T>::operator=(MutexGuard &&rhs) noexcept {
        if(this != &rhs) {
            unlock_impl();
            mutex_ = std::exchange(rhs.mutex_, nullptr);
        }
        return *this;
    }
}