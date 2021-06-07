#include <colite/coroutine/channel.hpp>

#include <folly/executors/ManualExecutor.h>
#include <gtest/gtest.h>

#include "folly_exec.hpp"
#include "task.hpp"

TEST(channel, immediate_send) {
    auto channel = colite::coroutine::channel::channel<int>();

    bool before_await = false;
    bool after_await = false;
    auto task = [&]() -> detail::task {
        before_await = true;
        co_await channel.sender.send(colite::executor::immediate_executor{}, 0);
        after_await = true;
    }();
    EXPECT_FALSE(before_await);
    EXPECT_FALSE(after_await);
    task.start_on(colite::executor::immediate_executor{});
    EXPECT_TRUE(before_await);
    EXPECT_TRUE(after_await);
    EXPECT_EQ(channel.receiver.available(), 1);
}

TEST(channel, immediate_send_receive) {
    auto channel = colite::coroutine::channel::channel<int>();

    int value_received = 0;
    auto task = [&]() -> detail::task {
        co_await channel.sender.send(colite::executor::immediate_executor{}, 20);
        value_received = (co_await channel.receiver.receive(colite::executor::immediate_executor{})).value();
    }();
    task.start_on(colite::executor::immediate_executor{});
    EXPECT_EQ(channel.receiver.available(), 0);
    EXPECT_EQ(value_received, 20);
    EXPECT_TRUE(task.is_done());
}

TEST(channel, inter_task_send_then_receive) {
    auto channel = colite::coroutine::channel::channel<int>();
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    auto sender = [exec](colite::coroutine::channel::sender_t<int> sender) -> detail::task {
        co_await sender.send(exec, 20);
    }(channel.sender);

    int value_received = 0;
    auto receiver = [exec, &value_received](colite::coroutine::channel::receiver_t<int> receiver) -> detail::task {
        value_received = *(co_await receiver.receive(exec));
    }(channel.receiver);

    sender.start_on(exec);
    receiver.start_on(exec);

    for (int i = 0; i < 10; i++) {
        folly_exec.run();
    }

    EXPECT_TRUE(sender.is_done());
    EXPECT_TRUE(receiver.is_done());
    EXPECT_EQ(value_received, 20);
}

TEST(channel, inter_task_receive_then_send) {
    auto channel = colite::coroutine::channel::channel<int>();
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    auto sender = [exec](colite::coroutine::channel::sender_t<int> sender) -> detail::task {
        co_await sender.send(exec, 20);
    }(channel.sender);

    int value_received = 0;
    auto receiver = [exec, &value_received](colite::coroutine::channel::receiver_t<int> receiver) -> detail::task {
        auto value = co_await receiver.receive(exec);
        value_received = value.value();
    }(channel.receiver);

    receiver.start_on(exec);
    sender.start_on(exec);

    for (int i = 0; i < 10; i++) {
        folly_exec.run();
    }

    EXPECT_TRUE(sender.is_done());
    EXPECT_TRUE(receiver.is_done());
    EXPECT_EQ(value_received, 20);
}

TEST(channel, multiple_send_receive) {
    auto channel = colite::coroutine::channel::channel<int>();
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    int expected_sum = 0;
    auto sender = [&]() -> detail::task {
        for (int i = 0; i < 10; i++) {
            expected_sum += i;
            co_await channel.sender.send(exec, i);
        }
    }();

    int sum_received = 0;
    auto receiver = [&]() -> detail::task {
        for (int i = 0; i < 10; i++) {
            auto received = co_await channel.receiver.receive(exec);
            sum_received += *received;
        }
    }();

    receiver.start_on(exec);
    sender.start_on(exec);

    while (!sender.is_done() || !receiver.is_done()) {
        folly_exec.run();
    }

    EXPECT_TRUE(sender.is_done());
    EXPECT_TRUE(receiver.is_done());
    EXPECT_EQ(sum_received, expected_sum);
}

TEST(channel, structured_binding) {
    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    int expected_sum = 0;
    auto sender_task = [sender, &expected_sum, exec]() mutable -> detail::task {
        for (int i = 0; i < 10; i++) {
            expected_sum += i;
            co_await sender.send(exec, i);
        }
    }();

    int received_sum = 0;
    auto receiver_task = [receiver, &received_sum, exec]() mutable -> detail::task {
        for (int i = 0; i < 10; i++) {
            auto received = co_await receiver.receive(exec);
            received_sum += *received;
        }
    }();

    receiver_task.start_on(exec);
    sender_task.start_on(exec);

    while (!sender_task.is_done() || !receiver_task.is_done()) {
        folly_exec.run();
    }

    EXPECT_TRUE(sender_task.is_done());
    EXPECT_TRUE(receiver_task.is_done());
    EXPECT_EQ(received_sum, expected_sum);
}

TEST(channel, closed_on_deleted_sender1) {
    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    std::optional<colite::coroutine::channel::sender_t<int>> wrapped_sender(std::move(sender));

    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    bool first_ok = false;
    bool empty_received = false;
    auto test_task = [&]() mutable -> detail::task {
        co_await wrapped_sender->send(exec, 0);
        auto value = co_await receiver.receive(exec);
        first_ok = value.has_value();
        wrapped_sender.reset();
        value = co_await receiver.receive(exec);
        empty_received = !value.has_value();
    }();

    test_task.start_on(exec);

    for (int i = 0; i < 100 && !test_task.is_done(); i++) {
        folly_exec.run();
    }

    EXPECT_TRUE(test_task.is_done());
    EXPECT_TRUE(first_ok);
    EXPECT_TRUE(empty_received);
}

TEST(channel, closed_on_deleted_sender2) {
    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    std::optional<colite::coroutine::channel::sender_t<int>> wrapped_sender(std::move(sender));

    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    bool started = false;
    bool empty_received = false;
    auto test_task = [&]() mutable -> detail::task {
        started = true;
        auto value = co_await receiver.receive(exec);
        empty_received = !value.has_value();
    }();

    test_task.start_on(exec);

    for (int i = 0; i < 3; i++) {
        folly_exec.run();
    }

    ASSERT_FALSE(test_task.is_done());
    ASSERT_TRUE(started);

    wrapped_sender.reset();
    for (int i = 0; i < 10; i++) {
        folly_exec.run();
    }

    EXPECT_TRUE(test_task.is_done());
    EXPECT_TRUE(empty_received);
}

TEST(channel, closed_on_deleted_receiver) {
    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    std::optional<colite::coroutine::channel::receiver_t<int>> wrapped_receiver(std::move(receiver));
    wrapped_receiver.reset();

    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });

    bool send_result = true;
    auto test_task = [&]() mutable -> detail::task {
        send_result = co_await sender.send(exec, 0);
    }();

    test_task.start_on(exec);

    for (int i = 0; i < 3; i++) {
        folly_exec.run();
    }

    EXPECT_TRUE(test_task.is_done());
    EXPECT_FALSE(send_result);
}

TEST(channel, destroy_task_before_receiver_wakeup) {
    tests::manual_executor exec;

    auto [sender, receiver] = colite::coroutine::channel::channel<int>();

    auto receive_task = std::make_unique<detail::task>([](colite::coroutine::channel::receiver_t<int> recv, tests::manual_executor exec) mutable -> detail::task {
        std::cout << "Awaiting receive" << std::endl;
        co_await recv.receive(exec);
        std::cout << "After receive" << std::endl;
    }(std::move(receiver), exec));

    receive_task->start_on(exec);

    // Receive task is started, waiting for data on the channel
    EXPECT_EQ(exec.run(), 1);

    auto send_task = [](colite::coroutine::channel::sender_t<int> sender, tests::manual_executor exec) mutable -> detail::task {
        std::cout << "Sending" << std::endl;
        co_await sender.send(exec, 10);
        std::cout << "After send" << std::endl;
    }(std::move(sender), exec);

    send_task.start_on(exec);
    // Send task is started, and send data to the channel
    // a wakeup for the receive task is pushed to the executor
    // but not run yet.
    EXPECT_EQ(exec.run(), 1);

    // Destroy the receive task
    receive_task.reset();
    // The wakeup is processed, but since the task is destroyed we
    // simply do nothing. Then send task is completed.
    EXPECT_EQ(exec.run(), 2);
    EXPECT_TRUE(send_task.is_done());
}

TEST(channel, destroy_task_pending_sender)
{
    tests::manual_executor exec;

    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    auto sender_task = std::make_unique<detail::task>([sender = std::move(sender), exec]() mutable -> detail::task {
        co_await sender.send(exec, 1);
    }());
    sender_task->start_on(exec);

    EXPECT_EQ(exec.run(), 1);
    sender_task.reset();
    EXPECT_EQ(exec.run(), 1);
}