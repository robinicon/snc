# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | ✅                 |
| older   | ❌                 |

## Reporting a Vulnerability

Please do **not** open a public issue for security vulnerabilities.

Report vulnerabilities to: **security@snc-framework.dev** (or the project maintainer's email).

Include:
- Description of the vulnerability
- Steps to reproduce
- Impact assessment (if known)

You will receive a response within 48 hours. Valid reports will be credited in the release notes unless you prefer anonymity.

## Security Practices

- All network payloads are treated as untrusted input; headers are validated before parsing.
- The framework uses C++17 smart pointers and RAII to minimize use-after-free and memory leak risks.
- RPC communication uses plain TCP — applications should run over TLS in production (not provided by this framework).
- AddressSanitizer is available for Debug builds via `SNC_ENABLE_SANITIZER=ON`.
