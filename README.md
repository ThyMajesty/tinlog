# tinlog

A tiny, header-only C++23 logging library. No dependencies beyond the standard library.

Runtime-configurable formatting, multiple independent sinks, terminal colors, source-location capture, and compile-time log-level filtering.

## Features

- Header-only - one file, `#include` and go.
- Compile-time level filtering via `TINLOG_LEVEL` - disabled levels compile to nothing.
- Per-sink formatting: timestamp, source location, colors, and overall message layout.
- Built-in sinks:
  - `TerminalSink` - stdout/stderr routing with automatic terminal color detection.
  - `FileSink` - file output with truncate or append modes.
  - `CallbackSink` - arbitrary user callback with both structured and rendered log data.
- `std::source_location` captured automatically at the call site.
- Multiple sinks can be active simultaneously, each with independent formatting.
- `NO_COLOR` and `TERM=dumb` are respected for automatic terminal color detection.
- Configurable timestamp time zone.

## Requirements

- C++23.
- A standard library implementation with the required `std::format`, `std::print`, `std::source_location`, and chrono time-zone support.

## Usage

### CMake integration

```cmake
add_subdirectory(path/to/tinlog)
target_link_libraries(your_target PRIVATE tinlog)
```

Or via `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(
    tinlog
    GIT_REPOSITORY https://github.com/thymajesty/tinlog.git
    GIT_TAG main
)

FetchContent_MakeAvailable(tinlog)

target_link_libraries(your_target PRIVATE tinlog)
```

### Basic example

```cpp
#include <tinlog.h>

int main() {
    tinlog::Log::addSink<tinlog::TerminalSink>();
    tinlog::Log::addSink<tinlog::FileSink>("logs/app.log");

    TINLOG_INFO("Starting up");
    TINLOG_WARN("Config value {} missing, using default", "timeout");
    TINLOG_ERROR("Failed to connect: {}", errorCode);
}
```

You can find a more thorough example in `main.cpp`.

### Log levels

The available levels are:

`TINLOG_TRACE`, `TINLOG_DEBUG`, `TINLOG_INFO`, `TINLOG_WARN`, `TINLOG_ERROR`, `TINLOG_CRITICAL`.

Set the compile-time threshold through `TINLOG_LEVEL` before including the header (or as a compile definition):

```cpp
#define TINLOG_LEVEL TINLOG_LEVEL_WARN
#include <tinlog.h>
```

Without an explicit value, it defaults to `TRACE` in debug builds and `INFO` when `NDEBUG` is defined.

**`TINLOG_LEVEL` should be set once, globally** (e.g. `target_compile_definitions` in CMake) and logging should be performed through the `TINLOG_*` macros if compile-time stripping is desired.

*You can always use `tinlog::Log::write` to bypass compile-time stripping.* 

### Sinks

Multiple sinks can be active at the same time:

```cpp
tinlog::Log::addSink<tinlog::TerminalSink>();
tinlog::Log::addSink<tinlog::FileSink>("logs/app.log");
```

`addSink` returns a non-owning pointer to the newly created sink. The library owns the sink. Calling `Log::removeSink(ptr)` destroys it and invalidates the pointer.

### Terminal output

`TerminalSink` routes messages to stdout or stderr according to a configurable threshold.

```cpp
auto* terminal = tinlog::Log::addSink<tinlog::TerminalSink>();

terminal->setStderrThreshold(tinlog::LogLevel::Error);
```

By default, levels below `ERROR` go to stdout and `ERROR` and above go to stderr.

Terminal colors use `ColorMode::Auto` by default. Automatic detection checks the target stream and also respects `NO_COLOR` and `TERM=dumb`.

Color behavior can be overridden per sink:

```cpp
terminal->format().color = tinlog::ColorMode::Always;
```

Available modes are:

- `ColorMode::Auto` - detect automatically.
- `ColorMode::Always` - always emit ANSI colors.
- `ColorMode::Never` - never emit colors.

### File output

`FileSink` creates missing parent directories automatically.

The default mode is `Truncate`:

```cpp
tinlog::Log::addSink<tinlog::FileSink>(
    "logs/app.log",
    tinlog::FileMode::Append
);
```

Available modes are `FileMode::Truncate` and `FileMode::Append`.

### Custom sinks

`CallbackSink` provides both the structured `LogMessage` and the fully rendered message:

```cpp
auto* cb = tinlog::Log::addSink<tinlog::CallbackSink>(
    [](const tinlog::LogMessage& msg, std::string rendered) {
        if (msg.level >= tinlog::LogLevel::Error)
            sendToCrashReporter(rendered);
    }
);
```

`LogMessage` contains:

- `level`
- `rawMessage`
- `source_location`
- `timestamp`

### Formatting

Every sink exposes a mutable `LogFormat` through `sink->format()`.

```cpp
sink->format().timestampFormat = "%Y-%m-%d %H:%M:%S";
sink->format().sourceFormat    = "{0}:{1}";
sink->format().pattern         = "{0}[{1}] [{2}] {3}{4} ({5})";
```

The `sourceFormat` arguments are:

- `{0}` - source file
- `{1}` - line
- `{2}` - function

The `pattern` arguments are:

- `{0}` - color escape
- `{1}` - rendered timestamp
- `{2}` - level name
- `{3}` - log message
- `{4}` - color reset escape
- `{5}` - rendered source location

The color escape and color reset escape are dependent on user specified color settings or Auto mode.

The timestamp is floored to milliseconds by default.

The time zone can be changed per sink:

```cpp
sink->format().timeZone = std::chrono::locate_zone("Europe/Kyiv");
```

`terminator` is appended after the rendered pattern and defaults to `"\n"`.

## License

MIT