#include "task.hpp"
#include <gtest/gtest.h>

#include <folly/executors/ManualExecutor.h>


inline void run_task(detail::task task)
{
    folly::ManualExecutor folly_exec_;

    task.start_on(colite::executor::adapt([&folly_exec_](auto fn) {
        fn();
    }));

    while(!task.is_done()) {
        folly_exec_.drive();
    }
}

TEST(coroutine, run_nonawaiting_task)
{
    bool was_run = false;
    run_task([&]() -> detail::task {
        was_run = true;
        co_return;
    }());
    EXPECT_TRUE(was_run);
}

TEST(coroutine, run_awaiting_task)
{
    bool before_await = false;
    bool awaited = false;
    bool after_await = false;

    auto to_await = [&]() -> detail::task {
        awaited = true;
        co_return;
    };

    auto task = [&]() -> detail::task {
        before_await = true;
        co_await to_await();
        after_await = true;
        co_return;
    };

    run_task(task());
    EXPECT_TRUE(before_await);
    EXPECT_TRUE(awaited);
    EXPECT_TRUE(after_await);
}