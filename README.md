# Shell-ish (COMP 304 Assignment 1 — Parts 1 & 2)

A simple Unix-like shell implementation built on the provided skeleton. This document describes the implemented features for **Part I** and **Part II**.

---

## Part I — Basic Shell

### Implemented features

- **Command parsing**  
  The skeleton parser is used to parse the input line into command name, arguments, and flags (background `&`, redirections, pipes).

- **Path resolution**  
  External commands are resolved via a custom path-search function:
  - If the command contains `/`, it is treated as a path and used as-is (if executable).
  - Otherwise, the `PATH` environment variable is searched; the first executable found is used.
  - The full path is passed to `execv()` so that programs run correctly.

- **Execution**  
  For external commands, the shell:
  1. Forks a child process.
  2. In the child: applies I/O redirection (Part 2), then resolves the path and runs the program with `execv()`.
  3. In the parent: waits for the child unless the command was run in the background.

- **Background processes**  
  If the user ends the line with `&`, the command is run in the background: the shell does not wait for it and immediately prints the next prompt.

- **Built-in commands**  
  - `exit` — exits the shell.  
  - `cd <directory>` — changes the current working directory (no `&` or piping for this built-in).

---

## Part II — I/O Redirection and Piping

### I/O redirection

- **`< file`** — standard input of the command is read from `file` instead of the terminal.
- **`> file`** — standard output is written to `file`; the file is created or truncated.
- **`>> file`** — standard output is appended to `file`; the file is created if it does not exist.

Redirection is implemented by opening the appropriate file(s) in the child process and using `dup2()` to replace stdin (fd 0) or stdout (fd 1) before calling `execv()`. The logic lives in **`io_redirection()`**, which is called for every external command (single or in a pipeline).


### Piping

- **`cmd1 | cmd2`** — standard output of `cmd1` becomes the standard input of `cmd2`.
- Longer chains work as well: **`cmd1 | cmd2 | cmd3`**.

Implementation outline:

- When the parser detects a `|`, it builds a linked list of commands (`command->next`).
- **`run_pipeline()`** is called with the first command. It works recursively:
  - For the **last** command: fork one process, connect its stdin to the read end of the previous pipe (if any), apply `io_redirection()`, then run the command.
  - For **earlier** commands: create a pipe, fork a child that writes to the pipe (stdout → pipe write end) and run the command; the parent closes the write end and recurses with the next command, passing the read end as its stdin.
- Background pipelines are supported: if the user ends the line with `&`, the shell does not wait for the pipeline to finish.


---

## File layout

- **`shellish-skeleton.c`** — Single source file containing:
  - Skeleton code (parsing, prompt, built-ins).
  - Part 1: path resolution (`res_cmd_path`), fork/exec, background handling.
  - Part 2: `io_redirection()`, `run_exec()`, `run_pipeline()`, and the wiring in `process_command()` for pipelines and redirection.


## Repo Link

- https://github.com/demirsamli/COMP304_assignment1.git
