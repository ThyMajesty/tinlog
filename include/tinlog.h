#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#define TINLOG_LEVEL_TRACE 0
#define TINLOG_LEVEL_DEBUG 1
#define TINLOG_LEVEL_INFO 2
#define TINLOG_LEVEL_WARN 3
#define TINLOG_LEVEL_ERROR 4
#define TINLOG_LEVEL_CRITICAL 5
#define TINLOG_LEVEL_OFF 6

#ifndef TINLOG_LEVEL
#ifdef NDEBUG
#define TINLOG_LEVEL TINLOG_LEVEL_INFO
#else
#define TINLOG_LEVEL TINLOG_LEVEL_TRACE
#endif
#endif

//TODO: threading, thread safety

namespace tinlog {

using LogLevel = int;

constexpr std::array<std::string_view, 6> kLevelNames = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT ",
};

constexpr std::array<std::string_view, 6> kLevelColors = {
    "\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[1;41;97m",
};

constexpr std::string_view levelColor(LogLevel lvl) {
    return kLevelColors[lvl];
}
constexpr std::string_view colorReset() {
    return "\033[0m";
}

struct LogMessage {
    LogLevel level;
    std::string rawMessage;
    std::source_location location;
    std::chrono::system_clock::time_point timestamp;
};

// Per-sink formatting config
// TODO: timestamp flooring to a specified precision instead of milliseconds (std::chrono::seconds, ..)
struct LogFormat {
    bool useColor = false;

    // chrono format spec, applied to the timestamp floored to milliseconds.
    std::string timestampFormat = "%Y-%m-%d %H:%M:%S";

    // Positional: {0} file, {1} line, {2} function.
    std::string sourceFormat = "File: {0} {2}:{1}";

    // Final assembly. Positional args are *already-rendered* pieces:
    //   {0} color escape (empty if useColor == false)
    //   {1} rendered timestamp
    //   {2} level name (padded)
    //   {3} user message
    //   {4} color reset escape (empty if useColor == false)
    //   {5} rendered source
    std::string pattern = "{0}[{1}] [{2}] {3}{4} ({5})";
};

class LogSink {
public:
    explicit LogSink(LogFormat format = {}) : m_format(std::move(format)) {}
    virtual ~LogSink() = default;

    virtual void write(const LogMessage& message) = 0;

    // Post-construction tuning, e.g.: sink->format().timestampFormat = "%Y-%m-%d %H:%M:%S";
    LogFormat& format() {
        return m_format;
    }
    const LogFormat& format() const {
        return m_format;
    }

protected:
    std::string render(const LogMessage& msg) const {
        std::string_view color = m_format.useColor ? levelColor(msg.level) : std::string_view {};
        std::string_view reset = m_format.useColor ? colorReset() : std::string_view {};
        std::string_view levelName = kLevelNames[msg.level];

        auto timestamp = std::chrono::floor<std::chrono::milliseconds>(msg.timestamp);
        std::string renderedTimestamp =
            std::vformat("{:" + m_format.timestampFormat + "}", std::make_format_args(timestamp));

        auto sourceFileName = msg.location.file_name();
        auto sourceLine = msg.location.line();
        auto sourceFunctionName = msg.location.function_name();
        std::string renderedSource =
            std::vformat(m_format.sourceFormat, std::make_format_args(sourceFileName, sourceLine, sourceFunctionName));

        return std::vformat(
            m_format.pattern,
            std::make_format_args(color, renderedTimestamp, levelName, msg.rawMessage, reset, renderedSource)
        );
    }

    LogFormat m_format;
};

// TODO: check the env we are currently in and strip color accordingly
class TerminalSink final : public LogSink {
public:
    explicit TerminalSink(LogFormat format = { .useColor = true }) : LogSink(std::move(format)) {}

    void write(const LogMessage& message) override {
        std::println("{}", render(message));
    }
};

// TODO: Proper aliasing
// TODO: Rotating files, backlog (/old/%timestamp%.log)
class FileSink final : public LogSink {
public:
    explicit FileSink(const std::filesystem::path& path, LogFormat format = {})
        : LogSink(std::move(format)), m_file(open(path)) {}

    void write(const LogMessage& message) override {
        std::println(m_file, "{}", render(message));
        m_file.flush();
    }

private:
    static std::ofstream open(const std::filesystem::path& path) {
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path);
        if (!file.is_open())
            throw std::runtime_error(std::format("FileSink: failed to open '{}'", path.string()));

        return file;
    }

    std::ofstream m_file;
};

class CallbackSink final : public LogSink {
public:
    using Callback = std::function<void(const LogMessage&, std::string)>;

    explicit CallbackSink(Callback callback, LogFormat format = {})
        : LogSink(std::move(format)), m_callback(std::move(callback)) {}

    void write(const LogMessage& message) override {
        // Additionally supply the rendered message - we have no idea how message will be used
        m_callback(message, render(message));
    }

private:
    Callback m_callback;
};

class Log {
public:
    Log() = delete;

    template <typename SinkT, typename... Args>
    static SinkT* addSink(Args&&... args) {
        auto sink = std::make_unique<SinkT>(std::forward<Args>(args)...);
        SinkT* ptr = sink.get();
        m_sinks.push_back(std::move(sink));
        return ptr;
    }

    static void removeSink(LogSink* sink) {
        std::erase_if(m_sinks, [sink](const auto& s) { return s.get() == sink; });
    }

    template <LogLevel Level, typename... Args>
    static void write(std::source_location location, std::format_string<Args...> fmt, Args&&... args) {
        // if constexpr (static_cast<int>(Level) >= TINLOG_LEVEL) {
        auto now = std::chrono::system_clock::now();
        std::string rawMessage = std::format(fmt, std::forward<Args>(args)...);

        LogMessage message {
            .level = Level,
            .rawMessage = std::move(rawMessage),
            .location = location,
            .timestamp = now,
        };

        for (auto& sink : m_sinks)
            sink->write(message);
        //}
    }

private:
    static inline std::vector<std::unique_ptr<LogSink>> m_sinks;
};

} // namespace tinlog

// Each macro supplies source_location::current() explicitly, evaluated at
// the call site, then forwards the format string + args straight through.
#if TINLOG_LEVEL <= TINLOG_LEVEL_TRACE
#define TINLOG_TRACE(...) tinlog::Log::write<TINLOG_LEVEL_TRACE>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_TRACE(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_DEBUG
#define TINLOG_DEBUG(...) tinlog::Log::write<TINLOG_LEVEL_DEBUG>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_DEBUG(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_INFO
#define TINLOG_INFO(...) tinlog::Log::write<TINLOG_LEVEL_INFO>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_INFO(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_WARN
#define TINLOG_WARN(...) tinlog::Log::write<TINLOG_LEVEL_WARN>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_WARN(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_ERROR
#define TINLOG_ERROR(...) tinlog::Log::write<TINLOG_LEVEL_ERROR>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_ERROR(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_CRITICAL
#define TINLOG_CRITICAL(...) tinlog::Log::write<TINLOG_LEVEL_CRITICAL>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_CRITICAL(...) (void)0
#endif