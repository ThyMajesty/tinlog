#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <print>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <cstdlib>

// We still have to define the numerical representation of each level manually,
// since we want to have preprocessor taking care of call sites definitions
#define TINLOG_LEVEL_TRACE 0
#define TINLOG_LEVEL_DEBUG 1
#define TINLOG_LEVEL_INFO 2
#define TINLOG_LEVEL_WARN 3
#define TINLOG_LEVEL_ERROR 4
#define TINLOG_LEVEL_CRITICAL 5
// Sentinel
#define TINLOG_LEVEL_OFF 6

// X(id, ID, NUM, name, color) - utility for enum typecheck, names and colors
#define TINLOG_LEVELS(X)                                                                                               \
    X(Trace, TRACE, TINLOG_LEVEL_TRACE, "TRACE", "\033[90m")                                                           \
    X(Debug, DEBUG, TINLOG_LEVEL_DEBUG, "DEBUG", "\033[36m")                                                           \
    X(Info, INFO, TINLOG_LEVEL_INFO, "INFO", "\033[32m")                                                               \
    X(Warn, WARN, TINLOG_LEVEL_WARN, "WARN", "\033[33m")                                                               \
    X(Error, ERROR, TINLOG_LEVEL_ERROR, "ERROR", "\033[31m")                                                           \
    X(Critical, CRITICAL, TINLOG_LEVEL_CRITICAL, "CRIT", "\033[1;41;97m")

// Either defined at compile-time/before tinlog.h included, or deduced the default from NDEBUG
#ifndef TINLOG_LEVEL
#ifdef NDEBUG
#define TINLOG_LEVEL TINLOG_LEVEL_INFO
#else
#define TINLOG_LEVEL TINLOG_LEVEL_TRACE
#endif
#endif

// TODO: threading, thread safety
namespace tinlog {

namespace color_details {
// Resolving env and adopting NO_COLOR for the terminal sink
// and maybe other streams
inline bool autoColorFor(std::FILE* f) {
    // https://no-color.org: present and non-empty disables color
    if (const char* nc = std::getenv("NO_COLOR"); nc && *nc)
        return false;
    // check for term
    if (const char* term = std::getenv("TERM"); term && std::string_view(term) == "dumb")
        return false;
#if defined(_WIN32)
    return _isatty(_fileno(f)) != 0;
#else
    return isatty(fileno(f)) != 0;
#endif
}
} // namespace color_details

enum class LogLevel : std::uint8_t {
#define X(id, ID, NUM, name, color) id = NUM,
    TINLOG_LEVELS(X)
#undef X
    // Sentinel
    Off
};

// We rely on sentinel being the last element of each representation
// Check the enumeration consistency, not needed tbh...
inline constexpr size_t kLevelCount = static_cast<size_t>(LogLevel::Off);
constexpr bool levelsAreDense() {
    size_t i = 0;
    bool ok = true;
#define X(id, ID, NUM, name, color) ok = ok && (static_cast<size_t>(NUM) == i++);
    TINLOG_LEVELS(X)
#undef X
    return ok;
}
static_assert(levelsAreDense(), "TINLOG_LEVELS must be dense and ordered from 0");
static_assert(TINLOG_LEVEL_OFF == kLevelCount, "TINLOG_LEVEL_OFF must follow the last level");

inline constexpr std::array<std::string_view, kLevelCount> kDefaultNames = {
#define X(id, ID, NUM, name, color) name,
    TINLOG_LEVELS(X)
#undef X
};

inline constexpr std::array<std::string_view, kLevelCount> kDefaultColors = {
#define X(id, ID, NUM, name, color) color,
    TINLOG_LEVELS(X)
#undef X
};

constexpr size_t lvl2idx(LogLevel lvl) {
    assert(lvl < LogLevel::Off && "invalid LogLevel");
    return static_cast<size_t>(lvl);
}
constexpr std::string_view getLevelName(LogLevel lvl) {
    return kDefaultNames[lvl2idx(lvl)];
}
constexpr std::string_view getLevelColor(LogLevel lvl) {
    return kDefaultColors[lvl2idx(lvl)];
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

enum class ColorMode : std::uint8_t { Never, Always, Auto };

// Per-sink formatting config
// TODO: timestamp flooring to a specified precision instead of milliseconds (std::chrono::seconds, ..)
struct LogFormat {
    ColorMode color = ColorMode::Auto;

    // chrono format spec, applied to the timestamp floored to milliseconds.
    std::string timestampFormat = "%Y-%m-%d %H:%M:%S";
    // cached timeZone - can be changed on the call site
    const std::chrono::time_zone* timeZone = std::chrono::current_zone();

    // Positional: {0} file, {1} line, {2} function.
    std::string sourceFormat = "File: {0} {2}:{1}";

    // Final assembly. Positional args are *already-rendered* pieces:
    //   {0} color escape (empty if color is disabled by ColorMode/env)
    //   {1} rendered timestamp
    //   {2} level name
    //   {3} user message
    //   {4} color reset escape (empty if color is disabled by ColorMode/env)
    //   {5} rendered source
    std::string pattern = "{0}[{1}] [{2:^5}] {3}{4} ({5})";

    // Appended after the rendered pattern. Not a part of the pattern, so
    // it never goes through std::vformat.
    std::string terminator = "\n";
};

class LogSink {
private:
    enum Field {
        Timestamp,
        Source,
        Pattern,
        // Sentilel
        FieldCount
    };
    static constexpr std::array<std::string_view, FieldCount> kFieldNames = { "timestampFormat",
                                                                              "sourceFormat",
                                                                              "pattern" };
    std::array<std::string, FieldCount> m_reportedFmtFields;

private:
    template <typename Fn>
    std::string
    guardedFormat(Field field, const std::string& spec, std::string& notices, Fn&& fn, std::string_view fallback = {}) {
        try {
            return fn();
        } catch (const std::format_error& e) {
            if (m_reportedFmtFields[field] != spec) {
                m_reportedFmtFields[field] = spec;
                std::format_to(
                    std::back_inserter(notices),
                    "[tinlog] invalid {} '{}': {}{}",
                    kFieldNames[field],
                    spec,
                    e.what(),
                    m_format.terminator
                );
            }
            return std::string(fallback);
        }
    }

protected:
    LogFormat m_format;

protected:
    // autoValue: what Auto means for the target about to be written to
    bool resolveColor(bool autoValue) const {
        switch (m_format.color) {
        case ColorMode::Always:
            return true;
        case ColorMode::Auto:
            return autoValue;
        case ColorMode::Never:
            return false;
        }
        return false;
    }

    // We have several overrides for the color
    // env vars such as NO_COLOR - will set autoValue for the color to false for terminal depending on the stream and
    // env characteristics. overrides per sink two levers ColorMode::Always/Never - user controlled - ignore all the
    // resolving logic.
    std::string render(const LogMessage& msg, bool useColor) {
        std::string_view color = useColor ? getLevelColor(msg.level) : std::string_view {};
        std::string_view reset = useColor ? colorReset() : std::string_view {};
        std::string_view levelName = getLevelName(msg.level);
        std::string fmtValidationMsgs;

        auto timestamp = std::chrono::floor<std::chrono::milliseconds>(msg.timestamp);
        auto zonedTimestamp = std::chrono::zoned_time { m_format.timeZone, timestamp };
        std::string renderedTimestamp =
            guardedFormat(Field::Timestamp, m_format.timestampFormat, fmtValidationMsgs, [&] {
                return std::vformat("{:" + m_format.timestampFormat + "}", std::make_format_args(zonedTimestamp));
            });

        auto sourceFileName = msg.location.file_name();
        auto sourceLine = msg.location.line();
        auto sourceFunctionName = msg.location.function_name();
        std::string renderedSource = guardedFormat(Field::Source, m_format.sourceFormat, fmtValidationMsgs, [&] {
            return std::vformat(
                m_format.sourceFormat,
                std::make_format_args(sourceFileName, sourceLine, sourceFunctionName)
            );
        });

        std::string out = guardedFormat(
            Field::Pattern,
            m_format.pattern,
            fmtValidationMsgs,
            [&] {
                return std::vformat(
                    m_format.pattern,
                    std::make_format_args(color, renderedTimestamp, levelName, msg.rawMessage, reset, renderedSource)
                );
            },
            msg.rawMessage // fallback
        );

        out += m_format.terminator;
        out += fmtValidationMsgs;
        return out;
    }

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
};

class TerminalSink final : public LogSink {
private:
    LogLevel m_stderrThreshold = LogLevel::Error;
    bool m_outColor;
    bool m_errColor;

public:
    explicit TerminalSink(LogFormat format = {})
        : LogSink(std::move(format)), m_outColor(color_details::autoColorFor(stdout)),
          m_errColor(color_details::autoColorFor(stderr)) {}

    // Messages with level >= threshold go to stderr, the rest to stdout.
    // LogLevel::Off = everything to stdout, LogLevel::Trace = everything to stderr.
    void setStderrThreshold(LogLevel lvl) {
        m_stderrThreshold = lvl;
    }

    void write(const LogMessage& message) override {
        const bool toErr = message.level >= m_stderrThreshold;
        if (toErr)
            // avoid misalignment between buffered stdout and unbuffered stderr
            std::fflush(stdout);
        std::print(toErr ? stderr : stdout, "{}", render(message, resolveColor(toErr ? m_errColor : m_outColor)));
    }
};

enum class FileMode : std::uint8_t {
    Truncate,
    Append,
};

// TODO: Proper aliasing
// TODO: Rotating files, backlog (/old/%timestamp%.log)
class FileSink final : public LogSink {
private:
    FileMode m_fileMode = FileMode::Truncate;
    std::ofstream m_file;

private:
    static std::ofstream open(const std::filesystem::path& path, FileMode fileMode) {
        std::error_code ec;
        if (path.has_parent_path() && !std::filesystem::create_directories(path.parent_path(), ec) && ec) {
            throw std::runtime_error(
                std::format("FileSink: failed to create directory '{}': {}", path.parent_path().string(), ec.message())
            );
        }

        auto openMode = std::ios::out;

        switch (fileMode) {
        case FileMode::Append:
            openMode |= std::ios::app;
            break;
        case FileMode::Truncate:
            openMode |= std::ios::trunc;
            break;
        }
        std::ofstream file(path, openMode);

        if (!file.is_open())
            throw std::runtime_error(std::format("FileSink: failed to open '{}'", path.string()));

        return file;
    }

public:
    explicit FileSink(const std::filesystem::path& path, FileMode fileMode = FileMode::Truncate, LogFormat format = {})
        : LogSink(std::move(format)), m_fileMode(fileMode), m_file(open(path, m_fileMode)) {}

    void write(const LogMessage& message) override {
        std::print(m_file, "{}", render(message, resolveColor(false)));
        m_file.flush();
    }
};

class CallbackSink final : public LogSink {
public:
    using Callback = std::function<void(const LogMessage&, std::string)>;

private:
    Callback m_callback;

public:
    explicit CallbackSink(Callback callback, LogFormat format = {})
        : LogSink(std::move(format)), m_callback(std::move(callback)) {}

    void write(const LogMessage& message) override {
        // Additionally supply the rendered message - we have no idea how the message will be used
        m_callback(message, render(message, resolveColor(false)));
    }
};

class Log {
private:
    static inline std::vector<std::unique_ptr<LogSink>> m_sinks;

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
};

} // namespace tinlog

// Each macro supplies source_location::current() explicitly, evaluated at
// the call site, then forwards the format string + args straight through.
#if TINLOG_LEVEL <= TINLOG_LEVEL_TRACE
#define TINLOG_TRACE(...) tinlog::Log::write<tinlog::LogLevel::Trace>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_TRACE(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_DEBUG
#define TINLOG_DEBUG(...) tinlog::Log::write<tinlog::LogLevel::Debug>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_DEBUG(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_INFO
#define TINLOG_INFO(...) tinlog::Log::write<tinlog::LogLevel::Info>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_INFO(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_WARN
#define TINLOG_WARN(...) tinlog::Log::write<tinlog::LogLevel::Warn>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_WARN(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_ERROR
#define TINLOG_ERROR(...) tinlog::Log::write<tinlog::LogLevel::Error>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_ERROR(...) (void)0
#endif

#if TINLOG_LEVEL <= TINLOG_LEVEL_CRITICAL
#define TINLOG_CRITICAL(...)                                                                                           \
    tinlog::Log::write<tinlog::LogLevel::Critical>(std::source_location::current(), __VA_ARGS__)
#else
#define TINLOG_CRITICAL(...) (void)0
#endif