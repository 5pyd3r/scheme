# Scheme

A self-hosting Scheme (R7RS) interpreter written in C with a bootstrapping compiler.

## Overview

~7,000 lines of C99 + ~750 lines of Scheme. Two compilers: a fast C compiler for
bootstrapping and a Scheme compiler (written in Scheme itself) for full R7RS
support. The C compiler loads the Scheme compiler as source and uses it as a
macro expander with fallback for forms it doesn't handle natively.

## Features

- Tagged-pointer VM with bytecode interpreter (40+ opcodes)
- Mark-and-sweep garbage collector with root-marker callback
- C bootstrap compiler: special forms (`if`, `lambda`, `let`, `let*`, `letrec`,
  `do`, `case`, `and`, `or`, `begin`, `set!`, `quasiquote`, `define`,
  `define-syntax`) plus 100+ primitive procedures
- Scheme compiler: `syntax-rules` macro system, pattern matching, closure
  capture with free-variable analysis
- First-class continuations (`call/cc`)
- Numeric tower: fixnum, flonum, bignum, rational, complex
- R7RS library: list/vector/string/char/bytevector utilities, `when`, `unless`
- Crash diagnostics: signal handlers with backtrace and VM state dump
- Debug build with ASAN/UBSAN support

## Build

```bash
meson setup build
meson compile -C build
meson test -C build      # 14 tests
```

## Usage

```bash
# REPL
./build/src/scheme

# Execute file
./build/src/scheme program.ss

# Debug build
meson setup build -Dbuildtype=debug
```

## Architecture

```
src/
  bootstrap/     C compiler (special forms, macro expansion)
  vm/            Bytecode VM, built-in dispatch, opcode definitions
  gc/            Mark-and-sweep GC
  reader/        S-expression parser
  primitives/    R7RS primitive procedures
  scheme/        Scheme compiler (compiler.scm) and library (lib.scm)
  pal/           Platform abstraction layer
  include/       Shared headers
```

## License

MIT
