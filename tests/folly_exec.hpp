#pragma once

#include <folly/executors/ManualExecutor.h>
#include <memory>

#include <concepts>

namespace tests
{
    class manual_executor
    {
        std::shared_ptr<folly::ManualExecutor> exec_;
    public:
        manual_executor(): exec_(std::make_shared<folly::ManualExecutor>()) {}
        manual_executor(const manual_executor&) = default;
        manual_executor(manual_executor&&) noexcept = default;
        ~manual_executor() {
            if(exec_ && exec_.use_count() == 1) {
                exec_->clear();
            }
        }

        void execute(std::invocable auto fn) const {
            exec_->add(std::move(fn));
        }
        std::size_t run() {
            return exec_->run();
        }

        friend bool operator==(const manual_executor& lhs, const manual_executor &rhs) noexcept {
            return lhs.exec_.get() == rhs.exec_.get();
        }
    };
}