# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | ✅                 |
| older   | ❌                 |

## Reporting a Vulnerability

Please do **not** open a public issue for security vulnerabilities.

Report vulnerabilities to: please open a GitHub Security Advisory at https://github.com/[REPO]/security/advisories/new

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

## ⚠️ Cryptographic Module Status

The `snc_auth` module is **not production-ready for authentication**:

- **SHA-256 / HMAC-SHA256 are not implemented** — the current implementations are non-functional placeholders.
- **Do NOT use `JwtService::Sign()` / `Verify()` in production** — generated signatures are invalid and cannot be verified.
- A proper, audited cryptographic implementation is planned for a future release.

See [CRYPTO_NOTICE.md](CRYPTO_NOTICE.md) for details.
