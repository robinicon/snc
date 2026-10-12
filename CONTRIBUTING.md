# Contributing to SNC

Thank you for your interest in contributing to SNC!

## How to contribute

1. **Fork** the repository and create a branch from `main`.
2. **Build** using CMake presets:
   ```bash
   cmake --preset=minimal
   cmake --build --preset=minimal
   ctest --preset=minimal
   ```
3. **Commit** following conventional commit style:
   ```
   fix(snc_core): resolve recv hang in pressure test
   feat(snc_rpc): add reconnect backoff config
   docs: update README architecture diagram
   ```
4. **Push** and open a Pull Request.

## Code style

- C++17, no C++20/23 features (to maximize compiler compatibility).
- Indent: 4 spaces.
- Line length: 120 characters.
- Use `enum class` over bare `enum`.
- Prefer `std::shared_ptr` / `std::make_shared` for objects crossing thread boundaries.

## Testing

All public APIs have unit tests in `libs/<lib>/tests/`. Run:

```bash
ctest --preset=normal --output-on-failure
```

For ASan builds (Linux only):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSNC_ENABLE_SANITIZER=ON
ctest --test-dir build -C Debug --output-on-failure
```

## Reporting issues

Please include:
- OS and compiler version
- CMake preset used
- Build output (last 50 lines)
- Test failure output (if applicable)
