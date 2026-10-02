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

void demoLevelOverrides() {
    section("per-level overrides");
    auto* sink = Log::addSink<TerminalSink>();

    // Base applies to every level without an override
    sink->format().pattern = "{0}{2:<5}{4} | {3}";
    sink->format().sourceFormat = "{1}";
    TINLOG_INFO("base pattern");
    TINLOG_WARN("base pattern");

    // Chained overrides: only Error and Critical change, the rest keep inheriting
    sink->format().pattern.clearOverrideForLevel(LogLevel::Critical).setBaseValue("[{1}] {2:<5} {3}");
    sink->format()
        .pattern.setOverrideForLevel(LogLevel::Error, "{0}!! {2} !!{4} {3} <{5}>")
        .setOverrideForLevel(LogLevel::Critical, "{0}!!! {2} !!! {3}{4} <{5}>");
    // Source part overridden separately: Critical also gets file and function
    sink->format().sourceFormat.setOverrideForLevel(LogLevel::Critical, "{0}:{1} in {2}");
    TINLOG_INFO("unchanged");
    TINLOG_ERROR("overridden pattern");
    TINLOG_CRITICAL("overridden pattern + source");

    // Base change propagates to non-overridden levels only
    sink->format().pattern = "{0}{2:<5}{4} >> {3}";
    TINLOG_INFO("base changed, follows it");
    TINLOG_ERROR("base changed, Error keeps its override");

    // Color: off for everything, forced on only for Warn/Error.
    // Always ignores NO_COLOR and redirects, so this shows even in a pipe.
    sink->format()
        .color.setBaseValue(ColorMode::Never)
        .setOverrideForLevel(LogLevel::Warn, ColorMode::Always)
        .setOverrideForLevel(LogLevel::Error, ColorMode::Always);
    TINLOG_INFO("no color");
    TINLOG_WARN("colored");
    TINLOG_ERROR("colored");

    // Per-level timestamp and terminator
    sink->format().pattern = "[{1}] {2:<5} {3}";
    sink->format()
        .timestampFormat.setBaseValue("%H:%M:%S")
        .setOverrideForLevel(LogLevel::Critical, "%Y-%m-%d %H:%M:%S");
    sink->format().terminator.setOverrideForLevel(LogLevel::Critical, "\n---\n");
    TINLOG_INFO("short time");
    TINLOG_CRITICAL("full date, separator after");

    // Query, then clear: Error goes back to inheriting the base pattern
    std::println(
        "Error pattern overridden: {}, Info: {}",
        sink->format().pattern.hasOverrideForLevel(LogLevel::Error),
        sink->format().pattern.hasOverrideForLevel(LogLevel::Info)
    );
    sink->format().pattern.clearOverrideForLevel(LogLevel::Error);
    sink->format().color.clearOverrideForLevel(LogLevel::Error);
    TINLOG_ERROR("Error is back on the base format");

    Log::removeSink(sink);
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

    sink->format().pattern = "{66}";
    sink->format().pattern.setOverrideForLevel(LogLevel::Error, "{77}");
    for (int i = 0; i < 3; ++i) {
        // notice only the first time for the same message and Level
        TINLOG_INFO("base bad");
        TINLOG_INFO("base bad");
        
        TINLOG_ERROR("level bad");
    }

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
    demoLevelOverrides();
    demoTerminator();
    demoSinks();
    // Deliberate throws
    demoBadFormat();
    demoFailure();
}