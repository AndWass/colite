#include <gtest/gtest.h>

#include "task.hpp"
#include "folly_exec.hpp"

#include <colite/sync/mutex.hpp>

TEST(mutex, lock1)
{
    tests::manual_executor exec;

    colite::sync::Mutex<int> mutex(5);
    bool locked = false;
    int value_stored = 0;
    auto task = [&]() -> detail::task {
        auto lock = co_await mutex.lock(exec);
        locked = true;
        value_stored = *lock;
    }();

    task.start_on(exec);

    for(int i=0; i<10; i++) {
        exec.run();
    }

    EXPECT_TRUE(locked);
    EXPECT_EQ(value_stored, 5);
}

TEST(mutex, lock2)
{
    tests::manual_executor exec;

    colite::sync::Mutex<int> mutex(0);

    int value_stored = 0;
    auto task1 = [&]() -> detail::task {
      auto lock = co_await mutex.lock(exec);
      value_stored = 1;
      *lock = 1;
    }();

    task1.start_on(exec);

    auto task2 = [&]() -> detail::task {
      auto lock = co_await mutex.lock(exec);
      value_stored = 2;
      *lock = 2;
    }();

    task2.start_on(exec);

    for(int i=0; i<10; i++) {
        exec.run();
    }

    EXPECT_EQ(value_stored, 2);
    EXPECT_TRUE(task1.is_done());
    EXPECT_TRUE(task2.is_done());
}

TEST(mutex, mutex_guard_arrow_op)
{
    tests::manual_executor exec;

    colite::sync::Mutex<std::string> mutex("");

    int value_stored = 0;
    auto task1 = [&]() -> detail::task {
      auto lock = co_await mutex.lock(exec);
      *lock = "Hello world";
    }();

    task1.start_on(exec);

    auto task2 = [&]() -> detail::task {
      auto lock = co_await mutex.lock(exec);
      value_stored = lock->size();
    }();

    task2.start_on(exec);

    for(int i=0; i<10; i++) {
        exec.run();
    }

    EXPECT_EQ(value_stored, 11);
    auto lock = mutex.try_lock();
    ASSERT_TRUE(lock.has_value());
    EXPECT_EQ(**lock, "Hello world");
    EXPECT_TRUE(task1.is_done());
    EXPECT_TRUE(task2.is_done());
}

TEST(mutex, destroy_while_awaiting)
{
    tests::manual_executor exec;

    colite::sync::Mutex<std::string> mutex("");
    auto lock = mutex.try_lock();

    ASSERT_TRUE(lock.has_value());

    auto task1 = std::make_unique<detail::task>([&]() -> detail::task {
      auto lock = co_await mutex.lock(exec);
    }());

    task1->start_on(exec);

    for(int i=0; i<10; i++) {
        exec.run();
    }

    EXPECT_FALSE(task1->is_done());
    task1.reset();
    lock->unlock();
}