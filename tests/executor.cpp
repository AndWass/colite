#include <gtest/gtest.h>

#include <colite/executor/executor.hpp>

TEST(executor, immediate_executor)
{
    colite::executor::ImmediateExecutor exec;
    bool first_run = false;
    bool second_run = false;
    colite::executor::execute(exec, [&] { first_run = true; });
    EXPECT_TRUE(first_run);
    EXPECT_FALSE(second_run);
    colite::executor::execute(exec, [&] { second_run = true; });
    EXPECT_TRUE(second_run);
}

TEST(executor, any_executor)
{
    colite::executor::AnyExecutor exec(colite::executor::ImmediateExecutor{});
    bool first_run = false;
    bool second_run = false;
    colite::executor::execute(exec, [&] { first_run = true; });
    EXPECT_TRUE(first_run);
    EXPECT_FALSE(second_run);
    colite::executor::execute(exec, [&] { second_run = true; });
    EXPECT_TRUE(second_run);
}

TEST(executor, adapt)
{
    auto exec = colite::executor::adapt([](std::invocable auto fn) {
        fn();
    });

    static_assert(colite::executor::Executor<decltype(exec)>, "adapt");
    bool first_run = false;
    bool second_run = false;
    colite::executor::execute(exec, [&] { first_run = true; });
    EXPECT_TRUE(first_run);
    EXPECT_FALSE(second_run);
    colite::executor::execute(exec, [&] { second_run = true; });
    EXPECT_TRUE(second_run);
}

#include <folly/executors/ManualExecutor.h>

TEST(executor, AdaptFollyExecutor)
{
    folly::ManualExecutor folly_exec;
    auto lite_exec = colite::executor::adapt([&folly_exec](auto fn) {
        folly_exec.add(std::move(fn));
    });
    bool first_run = false;
    bool second_run = false;
    colite::executor::execute(lite_exec, [&] { first_run = true; });
    colite::executor::execute(lite_exec, [&] { second_run = true; });
    EXPECT_FALSE(first_run);
    EXPECT_FALSE(second_run);;

    ASSERT_EQ(folly_exec.run(), 2);
    EXPECT_TRUE(first_run);
    EXPECT_TRUE(second_run);;
}