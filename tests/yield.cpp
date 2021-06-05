#include <colite/coroutine/yield.hpp>

#include <gtest/gtest.h>
#include "task.hpp"

#include <folly/executors/ManualExecutor.h>

TEST(yield, yields_to_same_executor)
{
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&](auto fn) {
        folly_exec.add(std::move(fn));
    });

    auto task = [exec]() -> detail::task {
        co_await colite::coroutine::yield(exec);
    }();

    task.start_on(exec);
    while(!task.is_done()) {
        folly_exec.drive();
    }
}

TEST(yield, yield_to_different_executor)
{
    folly::ManualExecutor folly_exec;
    auto exec = colite::executor::adapt([&](auto fn) {
      folly_exec.add(std::move(fn));
    });

    folly::ManualExecutor folly_exec2;
    auto exec2 = colite::executor::adapt([&](auto fn) {
        folly_exec2.add(std::move(fn));
    });


    bool before_yield = false;
    bool after_yield = false;
    auto task = [&]() -> detail::task {
        before_yield = true;
        co_await colite::coroutine::yield(exec2);
        after_yield = true;
    }();

    task.start_on(exec);

    auto run = [](folly::ManualExecutor& exec) {
      for(int i=0; i<10; i++) {
          exec.run();
      }
    };

    run(folly_exec);
    ASSERT_FALSE(task.is_done());
    ASSERT_TRUE(before_yield);
    ASSERT_FALSE(after_yield);

    run(folly_exec2);
    EXPECT_TRUE(task.is_done());
    EXPECT_TRUE(before_yield);
    EXPECT_TRUE(after_yield);
}