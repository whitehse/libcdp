# TODO.md — libcdp

## Current Status

The library is functionally complete for the initial release (v0.1.0).
All core features are implemented and tested.

## Planned Features

### Short-term (v0.2.0)

- [ ] Add `cdp_send_command_async` for non-blocking command queuing
- [ ] Support for CDP binary protocol (not just JSON)
- [ ] Add domain-specific event filtering
- [ ] Implement `cdp_get_targets` response parser
- [ ] Add `cdp_get_cookies` response parser

### Medium-term (v0.3.0)

- [ ] Support for CDP protocol domains:
  - [ ] Security
  - [ ] ServiceWorker
  - [ ] Storage
  - [ ] WebAudio
  - [ ] WebAuthn
- [ ] Add response caching (match command ID to response)
- [ ] Implement connection state tracking
- [ ] Add protocol version negotiation

### Long-term (v1.0.0)

- [ ] Full CDP protocol coverage
- [ ] Performance benchmarks
- [ ] Fuzzing coverage report
- [ ] Static analysis integration (clang-tidy, cppcheck)
- [ ] Documentation generation (Doxygen)
- [ ] Package manager support (vcpkg, conan)

## Known Issues

- JSON scanner doesn't handle escaped quotes in key names
- Queue overflow silently drops oldest events (could be configurable)
- No support for CDP's binary WebSocket frames
- Session ID is not validated (caller must ensure correctness)

## Testing Improvements

- [ ] Add stress tests for queue overflow
- [ ] Add performance tests for large message batches
- [ ] Add fuzzing for command builders
- [ ] Add integration tests with real Chrome (optional)
- [ ] Add Valgrind to CI pipeline

## Documentation

- [ ] Add man pages (man3) for all public functions
- [ ] Add Doxygen configuration
- [ ] Add usage examples for common workflows
- [ ] Add troubleshooting guide
