# tinlog

A tiny, header-only C++23 logging library. No dependencies beyond the standard library.

Compile-time level filtering, runtime-configurable output format per sink, and multiple sink types (terminal, file, custom callback) that can run simultaneously with independent formatting.

## Features

- Header-only — one file, `#include` and go.
- Compile-time level filtering via `TINLOG_LEVEL` — disabled levels compile to nothing.
- Per-sink format control: timestamp format, source-location format, and overall message layout are all independently configurable per-sink, at runtime.
- Built-in sinks: `TerminalSink` (colored), `FileSink`, `CallbackSink` (arbitrary user callback — GUI logs, network shipping, etc).
- `std::source_location` captured automatically at the call site.
- Multiple sinks active at once, each with its own format.

## Requirements

- C++23 (uses `std::format`, `std::print`, `std::source_location`).
- GCC 14+ or a compiler with equivalent `<print>`/`std::format` support.

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
    GIT_REPOSITORY https://github.com/yourname/tinlog.git
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
You can find a more thorough example in `main.cpp`

### Log levels

`TINLOG_TRACE`, `TINLOG_DEBUG`, `TINLOG_INFO`, `TINLOG_WARN`, `TINLOG_ERROR`, `TINLOG_CRITICAL`.

Set the compile-time threshold through `TINLOG_LEVEL` before including the header (or as a compile definition):

```cpp
#define TINLOG_LEVEL TINLOG_LEVEL_WARN
#include <tinlog.h>
```

Without an explicit value, it defaults to `TRACE` in debug builds and `INFO` when `NDEBUG` is defined.

**`TINLOG_LEVEL` should be set once, globally** (e.g. `target_compile_definitions` in CMake) and the logging performed through `TINLOG_*` macros, if you want to get compile time stripping.

*You can always use `tinlog::Log::write` whithout gating, though not advised.* 

### Custom sinks

```cpp
auto* cb = tinlog::Log::addSink<tinlog::CallbackSink>(
    [](const tinlog::LogMessage& msg, std::string rendered) {
        if (msg.level >= TINLOG_LEVEL_ERROR)
            sendToCrashReporter(rendered);
    });
```

`addSink` returns a non-owning pointer valid for the program's lifetime; the library owns and destroys the sink. Use `Log::removeSink(ptr)` to destroy one early.

### Formatting

Every sink exposes a mutable `LogFormat` via `sink->format()`:

```cpp
sink->format().timestampFormat = "%Y-%m-%d %H:%M:%S";
sink->format().sourceFormat    = "{0}:{1}"; // {0} file  {1} line  {2} function
sink->format().pattern         = "{0}[{1}] [{2}] {3}{4} ({5})";
// {0} color  {1} timestamp  {2} level  {3} message  {4} reset  {5} source
```

Timestamp precision is millisecond by default (`std::chrono::floor<std::chrono::milliseconds>` internally).

## License

MIT