# TODO.md — libcdp

## Current Status
The library is functionally complete for v0.1.0. All core features are implemented and tested.

**Completed (low/medium priority):**
- Queue overflow stress tests + robustness improvements
- Large batch performance tests
- Security, Storage, ServiceWorker, WebAudio, WebAuthn domain classification
- JSON escaped-quote scanner edge-case test

## Remaining Work

### Short-term (v0.2.0)
- [ ] `cdp_send_command_async`
- [ ] CDP binary protocol support
- [ ] Domain-specific event filtering
- [ ] `cdp_get_targets` / `cdp_get_cookies` response parsers

### Medium-term (v0.3.0)
- [ ] Response caching
- [ ] Connection state tracking
- [ ] Protocol version negotiation

### Long-term
- [ ] Full CDP coverage
- [ ] Performance benchmarks & fuzzing
- [ ] Static analysis (clang-tidy, cppcheck)
- [ ] Doxygen + man pages
- [ ] vcpkg / conan packaging

## Known Issues
- JSON scanner doesn't handle escaped quotes in key names
- Queue overflow drops oldest events (could be configurable)
- No binary WebSocket frame support
- Session ID not validated

## Testing & Docs
- [ ] Fuzzing for command builders
- [ ] Real Chrome integration tests
- [ ] Valgrind in CI
- [ ] Man pages, Doxygen, examples, troubleshooting guide
