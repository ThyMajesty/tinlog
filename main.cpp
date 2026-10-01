#include "include/tinlog.h"

#include <chrono>
#include <exception>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace tinlog;

namespace {

void section(std::string_view title) {
    std::println("\n=== {} ===", title);
}

void someFunctionThatReports() {
    std::string msg = "Hello from some function";
    TINLOG_CRITICAL("MSG: {0},", msg);
}

// Call sites compiled out by TINLOG_LEVEL expand to (void)0: arguments are never evaluated.
[[maybe_unused]] int expensive() {
    std::println("expensive() evaluated");
    return 42;
}

void demoLevels() {
    section("levels + format args");
    auto* sink = Log::addSink<TerminalSink>();

    int number = 1234;
    std::string_view text = "some text";
    double ratio = 3.14159265;

    // build with -DNDEBUG or
    // #define TINLOG_LEVEL TINLOG_LEVEL_DEBUG
    // to see it vanish
    TINLOG_TRACE("trace {}, lazy arg: {}", number, expensive());
    TINLOG_DEBUG("positional {1} {0}", number, text);
    TINLOG_INFO("plain message, no args");
    TINLOG_WARN("spec: [{:>12}] [{:<8.3f}] [{:#x}]", text, ratio, number);
    TINLOG_ERROR("errors go to stderr by default");
    TINLOG_CRITICAL("repeated {1} {0} {1} {0}", number, text);
    someFunctionThatReports();

    Log::removeSink(sink);
}

void demoStreams() {
    section("stderr threshold (try ./app 2>/dev/null or ./tinlog_example 1> output.txt 2> error.txt)");
    auto* sink = Log::addSink<TerminalSink>();

    // nothing to stderr
    sink->setStderrThreshold(LogLevel::Off);
    TINLOG_ERROR("error on stdout");

    sink->setStderrThreshold(LogLevel::Warn); // warn and up to stderr
    TINLOG_INFO("info on stdout");
    TINLOG_WARN("warn on stderr");

    sink->setStderrThreshold(LogLevel::Trace); // everything to stderr
    TINLOG_INFO("info on stderr");

    Log::removeSink(sink);
}

void demoColor() {
    section("color (NO_COLOR=1 or a redirect makes Auto drop the escapes)");
    auto* sink = Log::addSink<TerminalSink>();

    constexpr std::pair<ColorMode, std::string_view> modes[] = {
        { ColorMode::Never, "Never" },
        { ColorMode::Auto, "Auto" },
        { ColorMode::Always, "Always" },
    };
    for (auto [mode, name] : modes) {
        sink->format().color = mode;
        TINLOG_INFO("ColorMode::{}", name);
        TINLOG_ERROR("ColorMode::{}", name);
    }

    Log::removeSink(sink);
}

void demoFormatting() {
    section("per-sink formatting");
    auto* sink = Log::addSink<TerminalSink>();
    TINLOG_INFO("default format");

    sink->format().pattern = "{0}{2:<5}{4} | {3}";
    TINLOG_INFO("level + message only");

    sink->format().pattern = "{0}[{1}] [{2:^5}] {3}{4} <{5}>";
    // %S carries .mmm: the timestamp is floored to ms
    sink->format().timestampFormat = "%H:%M:%S";
    // line only
    sink->format().sourceFormat = "{1}";
    TINLOG_INFO("short timestamp, line only");

    sink->format().timeZone = std::chrono::locate_zone("UTC");
    sink->format().timestampFormat = "%Y-%m-%dT%H:%M:%SZ";
    sink->format().pattern = "{1} {2} {3}";
    TINLOG_INFO("UTC, no color, no source");

    Log::removeSink(sink);
}

void demoTerminator() {
    section("terminator");
    auto* sink = Log::addSink<TerminalSink>();
    sink->format().pattern = "{3}";
    sink->format().terminator = " | ";

    TINLOG_INFO("one");
    TINLOG_INFO("two");
    TINLOG_INFO("three");
    // the sink never emitted a newline, so we do
    std::println();

    Log::removeSink(sink);
}

void demoSinks() {
    section("one message, three sinks, three formats");

    auto* term = Log::addSink<TerminalSink>();
    term->format().pattern = "{0}{2:<5}{4} {3}";

    // overwritten each run, dir auto-created
    auto* truncating = Log::addSink<FileSink>("logs/truncate.log");
    // same but append
    auto* appending = Log::addSink<FileSink>("logs/append.log", FileMode::Append);
    appending->format().timestampFormat = "%F %T";
    appending->format().pattern = "{1} {2:<5} {3}";

    // stand-in for a GUI log widget
    std::vector<std::string> panel;
    auto* cb = Log::addSink<CallbackSink>([&panel](const LogMessage& msg, std::string rendered) {
        if (msg.level >= LogLevel::Warn)
            panel.push_back(std::move(rendered));
    });
    cb->format().pattern = "{2} {3} (line {5})";
    cb->format().sourceFormat = "{1}";
    cb->format().terminator = "";

    TINLOG_INFO("terminal and files only");
    TINLOG_WARN("everyone gets this one");
    TINLOG_ERROR("and this one");

    // The callback captures `panel` by reference: remove the sink before it dies.
    Log::removeSink(cb);
    Log::removeSink(term);
    Log::removeSink(truncating);
    Log::removeSink(appending);

    for (const auto& line : panel)
        std::println("panel: {}", line);
    std::println("see logs/truncate.log and logs/append.log (run twice: append grows)");
}

// Deliberate throw
void demoBadFormat() {
    section("invalid specs fall back instead of throwing");
    auto* sink = Log::addSink<TerminalSink>();

    // no argument 66
    sink->format().pattern = "{0}[{1}] {66}{4}"; 
    TINLOG_INFO("raw message + one notice");
    TINLOG_INFO("same bad pattern: no second notice");

    sink->format().pattern = "[{1}] {3}";
    // only valid for durations
    sink->format().timestampFormat = "%Q"; 
    TINLOG_INFO("timestamp renders empty, rest intact");

    Log::removeSink(sink);
}

// Deliberate throw
void demoFailure() {
    section("sink construction failure");
    try {
        // Linux: can't mkdir under /proc
        Log::addSink<FileSink>("/proc/tinlog/nope.log");
    } catch (const std::exception& e) {
        std::println("expected: {}", e.what());
    }
    // This will definitely crash!!!
    // Log::addSink<FileSink>("/proc/tinlog/nope.log");
}

} // namespace

int main() {
    demoLevels();
    demoStreams();
    demoColor();
    demoFormatting();
    demoTerminator();
    demoSinks();
    // Deliberate throws
    demoBadFormat();
    demoFailure();
}