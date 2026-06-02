# Security Policy

## Scope

Noxis OS is a **hobby kernel** and is not intended for production use.
That said, security issues in the kernel design (privilege escalation,
memory safety bugs, syscall validation bypasses) are still interesting
to report and will be addressed.

## Supported Versions

| Branch | Supported |
|---|---|
| `main` | ✅ Active |
| Older commits | ❌ No backports |

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Instead, report it privately:

1. Go to the **Security** tab on the GitHub repository.
2. Click **"Report a vulnerability"**.
3. Describe the issue with as much detail as possible:
   - Which subsystem is affected (mm, syscall, fs, …)
   - How to reproduce it
   - Potential impact (kernel panic, privilege escalation, …)

You will receive a response within **7 days**.

## What Counts as a Security Issue

- Kernel privilege escalation from ring-3 to ring-0
- Syscall argument validation bypass (`_user_range_ok` circumvention)
- Memory corruption in the kernel heap or page tables
- Use-after-free reachable from userland
- Page fault handler triggering a double fault

## What Does Not Count

- QEMU-specific quirks or emulation artefacts
- Intentional limitations documented in the README
- Crashes caused by deliberately malformed userland code
