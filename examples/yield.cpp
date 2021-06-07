#include <iostream>

#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/init/Init.h>

#include <colite/coroutine/yield.hpp>

auto folly_exec(folly::Executor *exec) {
    return colite::executor::adapt([exec](auto fn) {
      exec->add(std::move(fn));
    });
}

folly::coro::Task<void> first_task() {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (int i = 0; i < 10; i++) {
        std::cout << "First task " << i << "\n";
        co_await colite::coroutine::yield(exec);
    }
}

folly::coro::Task<void> second_task() {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (int i = 0; i < 10; i++) {
        std::cout << "Second task " << i << "\n";
        co_await colite::coroutine::yield(exec);
    }
}

int main(int argc, char **argv) {
    folly::init(&argc, &argv);
    auto first = first_task().scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto second = second_task().scheduleOn(folly::getGlobalCPUExecutor()).start();

    first.wait();
    second.wait();
}
