# Shell

## Project Overview
A lightweight, interactive UNIX shell built from scratch in C. It handles the full lifecycle of command execution — from reading user input with an interactive line editor, to tokenizing, parsing, and executing commands — including multi-stage pipelines and I/O redirection.


## Key Features
- **Interactive Line Editing** — A hand-written line editor providing interactive input handling, cursor movement, and history navigation without relying on external libraries like readline.
- **Pipeline Execution** — Forks child processes for each stage of a pipeline, wires them together with `pipe()` and `dup2()`, and waits for all children to complete.
- **Built-in Commands** — Implements `cd`, `pwd`, `echo`, `type`, and `exit` as shell built-ins that run in the parent process (or in forked children when part of a pipeline).
- **I/O Redirection** — Supports output redirection (`>`), append redirection (`>>`), input redirection (`<`), and file descriptor targeting (e.g., `2>` to redirect stderr).
- **PATH Resolution** — Searches `$PATH` directories to locate external executables.
- **Quoting Support** — A hand-written tokenizer that correctly handles single quotes, double quotes, backslash escapes, and mixed quoting within a single token.
- **Colorized Prompt** — Displays the current working directory in a two-line, color-coded prompt.

## Installation

### Prerequisites
- GCC, Clang, or any modern C compiler
- GNU Make

### Build Instructions
1. Clone the repository:
   ```bash
   git clone https://gihub.com/nileshpahari/shell
   cd shell
   ```
2. Build the project using the `Makefile`:
   ```bash
   make
   ```
3. Run the shell:
   ```bash
   make run
   # or run directly:
   ./shell
   ```

### Clean

```bash
make clean
```

## Project Structure

```
shell/
├── Makefile            # Build automation with auto-dependency tracking
├── README.md
├── include/            # Header files
│   ├── builtins.h      # Built-in command interface
│   ├── editor.h        # Custom editor interface
│   ├── executor.h      # Command execution interface
│   ├── helpers.h       # Utility function declarations
│   ├── lexer.h         # Token types and lexer interface
│   └── parser.h        # Pipeline and command structures
└── src/                # Source files
    ├── main.c          # Entry point — REPL loop
    ├── lexer.c         # Tokenizer (handles quoting, escapes, operators)
    ├── parser.c        # Builds pipeline structures from token streams
    ├── executor.c      # Forks processes, wires pipes, runs commands
    ├── builtins.c      # cd, pwd, echo, type, exit
    ├── helpers.c       # Prompt builder, PATH lookup, I/O redirection
    └── editor.c        # Custom line editor

```
