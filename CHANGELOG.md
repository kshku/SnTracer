# Changelog

## [0.2.2] - 2026-10-02

### Fixed
- sn_tracer_process() and sn_tracer_process_thread_buffer() passed -1 where the
  _n variants take a size_t. Converting a negative int to size_t is implementation
  defined rather than a plain wrap on every platform, and SIZE_MAX states the same
  intent at the type it is used at. The count is only ever used as an upper bound,
  so SIZE_MAX is unbounded in the same way
- SnTracerThreadBuffer.thread_id was int64_t while the hook that fills it returns
  uint64_t and the SnTracerEvent field it is copied into is uint64_t. The signed
  type was not meaningful: it round tripped through a value that cannot be
  negative, and a thread id with the high bit set was sign extended on the way out.
  It is uint64_t now, which is the same size and layout
- EVENT_VALIDITY_MASK was 1 << 15, an int, so ~EVENT_VALIDITY_MASK was a negative
  int that converted back to unsigned on every use. The bit pattern was right, the
  signedness was not

### Changed
- -Wconversion and -Wsign-conversion are on for gcc and clang

## [0.2.1] - 2026-09-28

### Changed
- Take sncore v0.3.1 rather than v0.2.0
- Take snmemory v0.3.2 rather than v0.2.0

## [0.2.0] - 2026-06-29

### Changed
- Updated the dependency versions

## [0.1.0] - 2026-06-11

- First release. See [0.0.0] section in CHANGELOG.md for full changelog.

## [0.0.0] - 2025-12-19

### Added
- Event tracing with timestamped records
- Event types: scope begin/end, instant, counter, flow begin/end/step, metadata
- User-provided ring buffer for lock-free event storage
- Per-thread processing with thread-local storage
- Chrome Trace JSON export for visualization (`chrome://tracing`)
- Metadata support (process name, thread name, custom key-value pairs)
- SnCore + SnMemory dependencies (ring buffer allocator)
- Multi-threaded test suite
- CI workflows (Linux, macOS, Windows, formatting)
