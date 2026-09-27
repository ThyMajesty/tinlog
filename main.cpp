#include "include/tinlog.h"

#include <string>

void some_function_that_reports() {
    std::string msg = "Hello from some function";
    TINLOG_CRITICAL("MSG: {0},", msg);
};

int main() {
    tinlog::Log::addSink<tinlog::TerminalSink>();

    auto* fileSink = tinlog::Log::addSink<tinlog::FileSink>("logs/app.log");

    auto* cbSink =
        tinlog::Log::addSink<tinlog::CallbackSink>([](const tinlog::LogMessage& message, std::string renderedMessage) {
            if (message.level >= TINLOG_LEVEL_ERROR)
                std::println("GUI sim callback: {}", renderedMessage);
        });

    cbSink->format().pattern = "{1} {3}{4} {5}";
    cbSink->format().timestampFormat = "%H:%M:%S";
    cbSink->format().sourceFormat = "from a file: {2}:{1} ";
    int test_number = 1234;
    std::string_view test_text = "some text";

    some_function_that_reports();

    TINLOG_TRACE("some trace message {}", test_number);
    TINLOG_DEBUG("some debug message {1} {0}", test_number, test_text);
    TINLOG_INFO("some info message");
    TINLOG_WARN("some warn message");
    TINLOG_ERROR("some error message");
    TINLOG_CRITICAL("some critical message {1} {0} {1} {0}", test_number, test_text);
}
