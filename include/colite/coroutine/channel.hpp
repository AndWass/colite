#pragma once

/**
 * @file
 * @brief Contains data and functions to create an asynchronous data channel
 *
 * Channels contains two parts: a sender and a receiver. Both are copyable, making it possible to create multiple senders (producers)
 * and multiple receivers (consumers).
 *
 * A sender can only be used to send data on a channel, while a receiver can only be used to receive data on a channel.
 * The number of senders and receivers alive for each channel is tracked, and a channel is closed when either all senders
 * or all receivers are destroyed.
 *
 * When a channel is closed, senders will not be able to send new data on the channel. Receivers will be able to read
 * all enqueued data, but will after that be notified that the channel is closed.
 */

#include <coroutine>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include <colite/executor/executor.hpp>

namespace colite::coroutine::channel {
    template<class T>
    struct channel_t;

    namespace detail {
        template<class T>
        struct waiting_receiver_t {
            std::coroutine_handle<> waiting_coro_;
            std::optional<T> value_;
            colite::executor::any_executor exec_{colite::executor::immediate_executor{}};
        };

        template<class T>
        struct state_t {
            std::mutex mutex_;
            std::deque<T> data_;
            std::vector<std::weak_ptr<waiting_receiver_t<T>>> waiting_receivers_;

            std::weak_ptr<void> sender_ticket_;
            std::weak_ptr<void> receiver_ticket_;

            std::optional<T> pop_value(const std::unique_lock<std::mutex> &) {
                if (data_.empty()) {
                    return std::nullopt;
                }
                auto retval = std::move(data_.front());
                data_.pop_front();
                return retval;
            }
        };
    }// namespace detail

    template<class T>
    class sender_t {
        using state_t = detail::state_t<T>;
        using waiting_receiver_t = detail::waiting_receiver_t<T>;

        template<class U>
        friend channel_t<U> channel();

        std::shared_ptr<state_t> state_;
        std::shared_ptr<void> ticket_;

        sender_t(std::shared_ptr<detail::state_t<T>> state, std::shared_ptr<void> ticket) noexcept
            : state_(std::move(state)), ticket_(std::move(ticket)) {
        }

        void wakeup_waiting_receivers(std::vector<std::weak_ptr<waiting_receiver_t>> waiting_receivers) {
            // We execute a function on each receivers associated executor.
            // This function, yet again, checks if data is available and if not
            // re-queues the receiver for wakeup again (unless the receiver is actually destroyed, then we do nothing).
            //
            // The alternative is to not probe all waiting receivers, but only the first
            // that we can lock on to. There might be a race where that
            // coroutine could potentially be destroyed between this function being enqueued for execution
            // and the execution actually taking place. If that happens then we enter a state where we
            // might have other readers waiting for data (maybe even all of them) and data is available,
            // but they haven't been woken up. To fix that we could potentially wakeup the next receiver
            // if our receiver is destroyed (and there is data available), but for now the approach to wakeup all receivers
            // is taken.
            for (auto weak_receiver : waiting_receivers) {
                if (auto receiver = weak_receiver.lock()) {
                    auto exec = receiver->exec_;

                    auto handler = [weak_receiver, state = this->state_] {
                        if (auto receiver = weak_receiver.lock()) {
                            // If the lock fails the receiver is actually destroyed.
                            std::unique_lock lock{state->mutex_};
                            auto maybe_value = state->pop_value(lock);
                            auto sender_ticket = state->sender_ticket_.lock();
                            if (maybe_value || !sender_ticket) {
                                lock.unlock();
                                receiver->value_ = std::move(maybe_value);
                                receiver->waiting_coro_.resume();
                            } else {
                                // No data and senders are still alive, re-add to list of waiting receivers
                                // to be woken up again in the future.
                                state->waiting_receivers_.push_back(receiver);
                            }
                        }
                    };
                    // Reset the receiver before executing the handler
                    // to ensure we don't accidentally keep it alive when
                    // handler is running.
                    receiver.reset();
                    colite::executor::execute(exec, std::move(handler));
                }
            }
        }

    public:
        sender_t(const sender_t &) = default;
        sender_t(sender_t &&) noexcept = default;
        ~sender_t() {
            if (ticket_.use_count() == 1) {
                ticket_.reset();
                // This class is the last holder of a ticket!
                state_->mutex_.lock();
                auto waiting_receivers = std::exchange(state_->waiting_receivers_, decltype(state_->waiting_receivers_){});
                state_->mutex_.unlock();
                wakeup_waiting_receivers(std::move(waiting_receivers));
            }
        }

        sender_t &operator=(const sender_t &) = default;
        sender_t &operator=(sender_t &&) noexcept = default;

        /**
         * @brief Asynchronously send data on the channel
         * @param exec The executor to resume on once data is sent.
         * @param value The value to send.
         * @return An `Awaitable<bool>`, indicating if channel is data was enqueued or not.
         *
         * If `co_await sender.send(exec, value)` returns `true` the channel is still open
         * and the data was successfully enqueued. Otherwise the channel is closed (all receivers are destroyed)
         * and no data was added to the channel.
         *
         */
        [[nodiscard]] auto send(colite::executor::executor auto exec, T value) {
            using exec_t = decltype(exec);
            struct awaitable {
                exec_t exec_;
                bool closed_ = false;
                static constexpr bool await_ready() noexcept {
                    return false;
                }

                void await_suspend(std::coroutine_handle<> to_suspend) noexcept {
                    colite::executor::execute(exec_, [to_suspend] {
                        to_suspend.resume();
                    });
                }

                [[nodiscard]] bool await_resume() const noexcept {
                    return !closed_;
                }
            };

            auto receiver_ticket = state_->receiver_ticket_.lock();
            if (!receiver_ticket) {
                return awaitable{std::move(exec), true};
            }

            auto push_and_steal_waiting_receivers = [&] {
                std::scoped_lock lock{state_->mutex_};
                state_->data_.push_back(std::move(value));
                return std::exchange(state_->waiting_receivers_, decltype(state_->waiting_receivers_){});
            };

            wakeup_waiting_receivers(push_and_steal_waiting_receivers());

            return awaitable{std::move(exec)};
        }
    };

    template<class T>
    class receiver_t {
        using state_t = detail::state_t<T>;
        using waiting_receiver_t = detail::waiting_receiver_t<T>;

        template<class U>
        friend channel_t<U> channel();

        std::shared_ptr<state_t> state_;
        std::shared_ptr<void> ticket_;

        receiver_t(std::shared_ptr<state_t> state, std::shared_ptr<void> ticket) noexcept
            : state_(std::move(state)), ticket_(std::move(ticket)) {
        }

    public:
        /**
         * @brief Get the available number of data to read from the channel.
         * @return
         *
         * @note This is a snapshot in time, it may not be accurate when used later.
         */
        [[nodiscard]] std::size_t available() const noexcept {
            std::scoped_lock lock{state_->mutex_};
            return state_->data_.size();
        }

        /**
         * @brief Asynchronously receive data from the channel.
         * @param exec The executor to resume on
         * @return An `Awaitable<std::optional<T>>`.
         *
         * The result of `co_await receiver.receive(some_exec)` is a `std::optional<T>`. If
         * the optional has no value set then the channel is closed; all senders have been destroyed
         * and no data is queued. Otherwise the optional contains the oldest enqueued data.
         */
        [[nodiscard]] auto receive(colite::executor::executor auto exec) {
            struct awaitable {
                std::shared_ptr<state_t> state_;
                std::shared_ptr<waiting_receiver_t> waiting_receiver_;

                static constexpr bool await_ready() noexcept {
                    return false;
                }

                bool await_suspend(std::coroutine_handle<> to_suspend) {
                    std::unique_lock lock{state_->mutex_};
                    auto sender_ticket = state_->sender_ticket_.lock();
                    if (state_->data_.empty()) {
                        if (!sender_ticket) {
                            waiting_receiver_->value_ = std::nullopt;
                            return false;
                        }
                        waiting_receiver_->waiting_coro_ = to_suspend;
                        state_->waiting_receivers_.push_back(waiting_receiver_);
                        return true;
                    }
                    waiting_receiver_->value_ = std::move(state_->data_.front());
                    state_->data_.pop_front();
                    return false;
                }

                std::optional<T> await_resume() {
                    return waiting_receiver_->value_;
                }
            };
            auto waiting_receiver = std::make_shared<waiting_receiver_t>();
            waiting_receiver->exec_ = std::move(exec);
            return awaitable{state_, std::move(waiting_receiver)};
        }
    };

    /**
     * @brief Return-type for `channel<T>()`.
     * @tparam T The type transported inside the channel.
     */
    template<class T>
    struct channel_t {
        sender_t<T> sender;
        receiver_t<T> receiver;
    };

    /**
     * @brief Create a new channel to send the specified type
     * @tparam T The type transported with the channel
     * @return The sender and receiver of the new channel.
     */
    template<class T>
    channel_t<T> channel() {
        auto state = std::make_shared<detail::state_t<T>>();
        auto sender_ticket = std::make_shared<char>(0);
        auto receiver_ticket = std::make_shared<char>(0);
        state->sender_ticket_ = sender_ticket;
        state->receiver_ticket_ = receiver_ticket;
        sender_t<T> sender(state, std::move(sender_ticket));
        receiver_t<T> receiver(std::move(state), std::move(receiver_ticket));
        return channel_t<T>{std::move(sender), std::move(receiver)};
    }
}// namespace colite::coroutine::channel