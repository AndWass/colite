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

folly::coro::Task<void> producer(colite::coroutine::channel::sender_t<int> sender) {
    auto exec = colite::executor::adapt(co_await folly::coro::co_current_executor);

    for(int i=0; i<10; i++) {
        std::cout << "Sending " << i << "\n";
        co_await sender.send(exec, i);
        co_await folly::coro::sleep(folly::Duration(200));
    }
}

folly::coro::Task<void> consumer(colite::coroutine::channel::receiver_t<int> receiver) {
    auto exec = colite::executor::adapt(co_await folly::coro::co_current_executor);

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
    auto [sender, receiver] = colite::coroutine::channel::channel<int>();
    auto prod = producer(std::move(sender)).scheduleOn(folly::getGlobalCPUExecutor()).start();
    auto cons = consumer(std::move(receiver)).scheduleOn(folly::getGlobalCPUExecutor()).start();

    prod.wait();
    cons.wait();
}