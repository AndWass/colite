#include <iostream>

#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/init/Init.h>

#include <colite/sync/channel.hpp>

auto folly_exec(folly::Executor *exec) {
    return colite::executor::adapt([exec](auto fn) {
        exec->add(std::move(fn));
    });
}

folly::coro::Task<void> producer(colite::mpmc::Sender<int> sender) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (int i = 0; i < 10; i++) {
        std::cout << "Sending " << i << "\n";
        co_await sender.send(exec, i);
        co_await folly::coro::sleep(folly::Duration(200));
    }
}

folly::coro::Task<void> consumer(colite::mpmc::Receiver<int> receiver) {
    auto exec = folly_exec(co_await folly::coro::co_current_executor);

    for (;;) {
        auto value = co_await receiver.receive(exec);
        if (!value.has_value()) {
            // null means channel is closed!
            break;
        }

        std::cout << "Received " << value.value() << "\n";
        co_await folly::coro::sleep(folly::Duration(500 + *value * 100));
    }
}

int main(int argc, char **argv) {
    folly::init(&argc, &argv);
    auto [sender, receiver] = colite::mpmc::channel<int>();
    auto prod = producer(std::move(sender)).scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto cons = consumer(std::move(receiver)).scheduleOn(folly::getGlobalCPUExecutor()).start();

    prod.wait();
    cons.wait();
}