# colite

A playground coroutine-library for coroutine-to-coroutine utilities. It is
meant to make a minimal set of assumptions as to how tasks and executors behave.
It doesn't assume that a task is well-behaved in that it always
ensures that the task is resumed on the correct executor. This is why
all awaitable-producing functions take an `executor` as an argument.

**This project is a playground. Do not expect production-quality!**

## Table of contents

  * [Executor](#executor)
  * [Mutex](#mutex)
  * [Channel](#channel)
  * [Yield](#yield)

## Executor

The `executor` concept is taken from [P0443](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r13.html)

### execute(exec, fn)

This function object invokes a callable on the provided executor, either via the `exec.execute` member function, or
via ADL.

### immediate_executor

A minimal executor that simply calls the provided callable immediately.

### any_executor

A type-erase helper for executors. Can hold any executor that satisfies the `executor' concept.

### adapt

`adapt(adaptable auto)` is a helper function that takes a copyable and movable invocable which must be callable with
`std::function<void()>` and forwards this callable to the real executor.

This allows 

#### Example
```cpp
folly::ManualExecutor folly_exec;
auto exec = colite::executor::adapt([&folly_exec](auto fn) {
    folly_exec.add(std::move(fn));
});

colite::executor::execute(exec, [] {
    std::cout << "Hello world" << std::endl;
});

folly_exec.run(); // Prints "Hello world"
```

## Mutex

A mutex provides synchronization of values between tasks. Unlike `std::mutex` this `mutex`
holds the guarded value within the mutex. Locking the mutex returns a guard that can be used to access the value
and unlocking the mutex again. A guard is automatically unlocked when destroyed.

For instance to provide mutually exclusive access to a string one would use a `colite::sync::mutex<std::string> mutex`

## Example

```cpp
#include <iostream>

#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/init/Init.h>

#include <colite/sync/mutex.hpp>
#include <colite/task/yield.hpp>

auto folly_exec(folly::Executor *exec) {
    return colite::executor::adapt([exec](auto fn) {
      exec->add(std::move(fn));
    });
}

folly::coro::Task<void> first_task(colite::sync::mutex<int>& mutex) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);
    auto lock = co_await mutex.lock(exec);
    for (int i = 0; i < 5; i++) {
        *lock += i;
        std::cout << "First task " << i << "\n";
        co_await colite::task::yield(exec);
    }
}

folly::coro::Task<void> second_task(colite::sync::mutex<int>& mutex) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);
    auto lock = co_await mutex.lock(exec);
    for (int i = 0; i < 5; i++) {
        *lock += i;
        std::cout << "Second task " << i << "\n";
        co_await colite::task::yield(exec);
    }
}

int main(int argc, char **argv) {
    folly::init(&argc, &argv);
    colite::sync::mutex<int> mutex(0);
    auto first = first_task(mutex).scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto second = second_task(mutex).scheduleOn(folly::getGlobalCPUExecutor()).start();

    first.wait();
    second.wait();

    auto value = mutex.try_lock();
    std::cout << "Value when done = " << **value << "\n";
}
```

## Channel

A channel contains two parts: a sender and a receiver. Both are copyable, making it possible to create multiple senders (producers)
and multiple receivers (consumers).

A sender can only be used to send data on a channel, while a receiver can only be used to receive data on a channel.
The number of senders and receivers alive for each channel is tracked, and a channel is closed when either all senders
or all receivers are destroyed.

When a channel is closed, senders will not be able to send new data on the channel. Receivers will be able to read
all enqueued data, but will after that be notified that the channel is closed.

### Example

```cpp
#include <iostream>

#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/init/Init.h>

#include <colite/coroutine/channel.hpp>

auto folly_exec(folly::Executor* exec) {
    return colite::executor::adapt([exec](auto fn) {
        exec->add(std::move(fn));
    });
}

folly::coro::Task<void> producer(colite::sync::mpmc::sender_t<int> sender) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for(int i=0; i<10; i++) {
        std::cout << "Sending " << i << "\n";
        co_await sender.send(exec, i);
        co_await folly::coro::sleep(folly::Duration(200));
    }
}

folly::coro::Task<void> consumer(colite::sync::mpmc::receiver_t<int> receiver) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for(;;) {
        auto value = co_await receiver.receive(exec);
        if(!value.has_value()) {
            // null means channel is closed!
            break;
        }

        std::cout << "Received " << value.value() << "\n";
        co_await folly::coro::sleep(folly::Duration(500 + *value*100));
    }
}

int main(int argc, char** argv) {
    folly::init(&argc, &argv);
    auto [sender, receiver] = colite::sync::mpmc::channel<int>();
    auto prod = producer(std::move(sender)).scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto cons = consumer(std::move(receiver)).scheduleOn(folly::getGlobalCPUExecutor()).start();

    prod.wait();
    cons.wait();
}
```

## Yield

This is an awaitable that "yields" once to the executor. It causes the current
coroutine to re-schedule itself for execution on the supplied executor.

This can be useful if a coroutine needs to do time-consuming non-async processing
to allow other coroutines to make progress once in a while.

### Example

```cpp
#include <iostream>

#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/init/Init.h>

#include <colite/task/yield.hpp>

auto folly_exec(folly::Executor *exec) {
    return colite::executor::adapt([exec](auto fn) {
      exec->add(std::move(fn));
    });
}

folly::coro::Task<void> first_task() {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (int i = 0; i < 10; i++) {
        std::cout << "First task " << i << "\n";
        co_await colite::task::yield(exec);
    }
}

folly::coro::Task<void> second_task() {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (int i = 0; i < 10; i++) {
        std::cout << "Second task " << i << "\n";
        co_await colite::task::yield(exec);
    }
}

int main(int argc, char **argv) {
    folly::init(&argc, &argv);
    auto first = first_task().scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto second = second_task().scheduleOn(folly::getGlobalCPUExecutor()).start();

    first.wait();
    second.wait();
}
```

