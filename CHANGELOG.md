# Changelog

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
