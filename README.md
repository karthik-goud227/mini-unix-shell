# Mini-Unix Shell (msh)

An educational Unix shell implemented in C for a third-year CSE
Operating Systems project.

The project implements a defined subset of shell behavior, with
interactive job control and signal handling as its main focus.
It runs on Linux, including Ubuntu inside Windows WSL 2.

## Base paper

S. R. Bourne, "The UNIX Shell," The Bell System Technical Journal,
vol. 57, no. 6, July-August 1978, pp. 1971-1990.

The paper provides the conceptual foundation for command execution,
pipes, redirection and shell behavior. This project is an independent
educational implementation of a subset, not a reproduction of the
complete Bourne shell.

Background execution and signal handling already appear in the paper.
This project's extension focuses on process-group job management,
foreground terminal ownership, stopped-job resumption and testing.

Modern implementation reference:
https://sourceware.org/glibc/manual/latest/html_node/Implementing-a-Shell.html

## Requirements

- Linux or Windows with WSL 2 and Ubuntu
- GCC with C11 support
- GNU Make
- Python 3 for integration tests
- Standard Linux utilities for testing
- An interactive terminal for job-control tests
- Optional: Git, GDB and Valgrind

Verified development environment:
Ubuntu 24.04.3 LTS on WSL 2, GCC 13.3.0 and GNU Make 4.3.

## Build and run

```bash
make
./msh
```

Run these commands from the repository directory.
Type commands at the `msh>` prompt.

To remove generated executables and object files:

```bash
make clean
```

## Supported features

- External programs with arguments and PATH lookup
- Built-ins: cd, pwd, exit, jobs, fg, bg, stats, export, unset and history
- Single and double quotes for argument grouping
- Environment-variable expansion with $NAME
- Environment-variable assignment and removal with export/unset
- Previous command status expansion with $?
- Empty quoted arguments and adjacent quoted/unquoted fragments
- Backslash escaping
- Input redirection: <
- Output overwrite: >
- Output append: >>
- Multiple redirections applied in source order
- Concurrent pipelines with multiple stages
- Sequential command lists separated by semicolons
- Conditional execution with && and ||
- Background pipelines separated by &, including a trailing &
- Background logical chains managed as one job
- Process-group foreground/background job management
- Ctrl+C for foreground interruption and prompt cancellation
- Ctrl+Z for stopping foreground jobs
- SIGTTIN behavior for background terminal readers
- Terminal-mode preservation across stop and resume
- SIGCHLD-driven background reaping while idle
- Confirmed shutdown with active-job cleanup
- Per-job CPU and memory statistics with bounded session history
- Interactive command history with numbering and clear support

## Examples

Enter these inside msh:

```text
echo "hello world"
pwd
cd /tmp
echo first > example.txt
echo second >> example.txt
cat < example.txt
printf "apple\nbanana\n" | grep banana | wc -l
sleep 30 &
jobs
fg %1
```

Replace `%1` with the actual job ID.
Job IDs increase during a shell session and need not be consecutive
among the jobs currently listed.

## Built-in behavior

- `cd [path]`: changes the shell's directory; no path uses HOME.
- `pwd`: prints the physical current directory; accepts no arguments.
- `exit [status]`: exits with a decimal status from 0 through 255.
  Without an argument, it preserves the previous status.
  Invalid arguments report an error and keep the shell running.
- `jobs`: lists tracked jobs; accepts no arguments.
- `stats`: displays saved completed-job statistics; accepts no arguments.
- `export NAME=value`: sets or replaces one environment variable.
- `unset NAME`: removes one environment variable.
- `history`: displays retained commands from oldest to newest.
- `history -c`: clears command history and restarts numbering.
- `fg %N`: places job N in the foreground and resumes it if stopped.
- `bg %N`: resumes job N without giving it the terminal.

All built-ins run in the parent for standalone foreground commands.
Only pwd is permitted inside ordinary pipelines or single-command
background jobs.

Inside a multi-item background logical chain, standalone cd, pwd,
export, unset and exit run in the supervisor. Directory and environment
changes affect only that supervisor and its future children.
exit ends the chain. Only pwd is permitted in multi-stage pipelines.
jobs, fg, bg, stats and history are rejected inside the chain.
Redirection is supported for cd, pwd, exit, jobs, stats, export,
unset and history.
Redirection on fg and bg is deliberately rejected.

Job-control built-ins and background execution require an interactive
terminal. Non-interactive foreground commands and pipelines are supported.

## Command lists and conditional execution

Separate commands or pipelines with an unquoted semicolon:

```text
echo first; echo second
echo hello | cat; echo done
false; echo $?
export MSH_LIST_TEST=hello; echo "$MSH_LIST_TEST"
```

Items execute in order. An ordinary command failure does not prevent the
next item from running. The list's final status is the status of its last
executed item.

A trailing semicolon is allowed. Leading or repeated semicolons are
rejected. Quoted or escaped semicolons remain literal argument text.

The entire input line is lexed and syntax-checked before execution.
A syntax error in a later item prevents earlier items from running.
This does not precheck runtime failures such as missing commands or files.

Expansion happens separately before each pipeline executes. Later items
therefore see environment changes and exit statuses from earlier items.
For example, false; echo $? prints 1.

A valid exit request stops the remaining items. With active jobs, the
existing interactive confirmation warning still applies, and the rest
of that line is skipped. Invalid exit arguments report an error and allow
the next item to run.

History records the whole submitted line as one entry. Ordinary jobs
use each pipeline's original command text. A background logical-chain
job uses the original text of the whole chain.

Logical operators control whether the next pipeline executes:

- a && b: execute b only if a succeeds (status 0).
- a || b: execute b only if a fails (nonzero status).
- Both operators have equal precedence and associate left to right.
- A semicolon starts an unconditional next item.
- Skipped items do not expand words, apply redirections, run built-ins
  or change the previous status.

For example, true || echo skipped && echo final prints final.
The list returns the status of the last pipeline actually executed.
A pipeline's last stage determines its status.

The entire line is syntax-checked, including skipped items.
A trailing && or || is rejected before any commands execute.

An unquoted & backgrounds the preceding unconditional pipeline and
allows the next list item to run immediately. Background execution
currently requires an interactive terminal.

Examples:
- sleep 5 & echo started
- sleep 5 & sleep 10 & echo both-started
- echo hello | cat & echo continued

Each background pipeline has its own job ID and command text.
The following item starts unconditionally and may begin a logical chain.
A trailing & is allowed. Leading &, repeated separated ampersands,
and & immediately followed by a semicolon are syntax errors.

An & after a logical chain backgrounds the entire chain.
For example: sleep 2 && echo finished & echo started

The parent continues immediately. One supervisor evaluates the chain.
Its pipeline stages share one process group, and the parent tracks
the whole chain as one job with its original command text.

The supervisor applies logical conditions and expands each executed
pipeline using its own environment and previous status.
Skipped pipelines do not expand words or apply redirections.

fg and bg operate on the whole chain. Ctrl+Z stops it. Ctrl+C after
fg interrupts it without executing the remaining chain commands.
Successful launch returns 0; fg returns the chain's eventual status.
Background execution requires an interactive terminal.

## Variable expansion

The shell first lexes and validates the unexpanded command list.
Before each pipeline executes, its tokens are expanded and parsed
into executable commands. Original token spelling preserves quote
and escape boundaries.

- `$NAME` reads the shell's current environment, including inherited
  variables and values set using export.
- `$?` expands to the previous command's status.
- Double quotes allow expansion; single quotes prevent it.
- A backslash before `$` prevents expansion outside quotes and inside
  double quotes.
- Variable names use letters, digits and underscores, and cannot
  start with a digit.
- Unset variables expand to an empty string.
- Expansion applies to command words, arguments and redirection paths.

Examples inside msh:

```text
echo "$HOME"
echo '$HOME'
echo \$HOME
false
echo $?
```

The first command prints the home directory. The next two print
literal `$HOME`. After `false`, `echo $?` prints `1`.

Each original word remains one argument, including empty values.
There is no field splitting or wildcard expansion, even for unquoted
variables. This intentionally differs from full POSIX shell behavior.

Inserted values are not expanded again or interpreted as shell syntax.
For example, a pipe character inside a variable does not create a pipeline.
All `$?` references within one pipeline use the status from before
that pipeline. A later executed list item sees the status left by
the last executed item; skipped items leave that status unchanged.

Only `$NAME` and `$?` are supported. Braced expansion such as `${HOME}`,
positional parameters such as `$1`, and special parameters such as `$$`
are not implemented. Bare assignments such as NAME=value and
per-command assignments such as NAME=value command are not supported;
use export NAME=value instead.

## Environment built-ins

Supported forms:

```text
export MSH_DEMO=hello
export MSH_DEMO="hello world"
printenv MSH_DEMO
unset MSH_DEMO
```

export sets or replaces exactly one variable in the shell's environment.
Future child programs inherit it. unset removes exactly one variable;
removing an absent variable succeeds.

Names must match `[A-Za-z_][A-Za-z0-9_]*`. Values may be empty, contain
spaces when quoted, or contain additional equals signs. The first equals
sign separates the name from the value.

An empty value is different from an absent variable: export NAME=
keeps NAME present with an empty value, while unset NAME removes it.

Expansion happens before the built-in runs. For example:

```text
export MSH_DEMO=hello
export MSH_DEMO="$MSH_DEMO world"
```

The second assignment uses the previous value and stores "hello world".

Both commands run in the parent for standalone foreground use and work
interactively or through non-interactive input. They are rejected in
ordinary pipelines and single-command background jobs. Standalone use
inside a multi-item background logical chain changes only the
supervisor's environment.
Redirection is supported; if redirection setup fails, the environment
change is not performed.

Successful operations return 0, invalid names or usage return 2, and
operational failures return 1. Changes affect this shell and future
children, not its parent terminal shell or already-running children.
The commands do not write persistent configuration.

This version does not support multiple assignments/names in one command,
export without an assignment, environment listing through export, options,
or a separate set of unexported shell variables.

## Command history

Interactive sessions retain the latest 100 nonblank submitted input lines.
Entries are stored before lexing and expansion, preserving original
quotes, escapes and variable references.

Example inside a fresh msh session:

```text
echo first
echo "$HOME"
history
history -c
history
```

The first history command lists three entries, including itself.
After clearing, the next history command appears as entry 1.

- Blank or whitespace-only lines are skipped.
- Input cancelled with Ctrl+C is not stored.
- Lines rejected by the input reader, such as oversized lines, are not stored.
- Submitted lines with syntax or execution errors are retained.
- Repeated commands are retained as separate entries.
- When full, history discards the oldest entry; numbering keeps increasing.
- history -c clears all entries, including its own submitted line.
- History is held in memory and freed at shutdown; it is not saved to disk.

history supports output redirection, for example `history > commands.txt`.
The redirected history command is included in the saved output.
If redirection setup fails for history -c, existing history is not cleared,
although that submitted line has already been recorded.

The built-in is rejected in pipelines and background jobs. It accepts
only no arguments or a single -c argument; invalid usage returns 2.
Non-interactive input is not recorded, so history normally prints nothing
in a non-interactive shell.

If recording fails, the shell reports the error and still processes the
command. History is separate from stats: it records submitted input,
while stats stores completed-job resource summaries.

This version does not implement history recall such as !! or !N,
arrow-key navigation, search, or automatic persistence across sessions.

## Job resource statistics

Run `stats` to display up to 16 saved completed-job summaries,
newest archived entry first. History is held in memory for the current
shell session and is cleared when the shell exits.

Example inside msh:

```text
sleep 1
echo hello | cat
stats
stats > statistics.txt
```

Each entry contains:

- Job ID and original command text.
- Measured processes / total processes.
- User CPU time summed across measured stages.
- System CPU time summed across measured stages.
- Largest individual process maximum resident set size, in KiB.

One KiB is 1024 bytes. CPU time measures processor work, not elapsed
time: a sleeping command can take one second while using very little CPU.

The memory value is the largest reported process peak. It is not
the sum of process peaks or the simultaneous peak memory of a pipeline.
Measurements follow Linux wait4 accounting and are not a live memory
monitor or a process-tree profiler.

For a background logical chain, the parent measures the supervisor
as one tracked process. Linux accounting can include children that
the supervisor has waited for. A displayed process count of 1/1
therefore describes the supervisor record, not the number of commands
or pipeline stages executed. These entries do not provide separate
per-stage measurements.

The shell collects resource usage with wait4 and saves final measurements
only on termination. Stops and continued events do not finalize usage.
Completed summaries are archived when their job records are removed;
ordering therefore reflects archival order, not precise exit timestamps.

Foreground, background and signal-terminated jobs can appear in history.
Running and stopped jobs do not appear. A completed job can remain in
statistics after it is no longer available to fg or bg.

Parent-executed built-ins, including stats itself, do not create job
entries. stats works in interactive and non-interactive sessions, supports
redirection, and is rejected inside pipelines or background jobs.

Entries with missing measurements show their coverage. Partial values
represent only measured processes; unavailable measurements are not
presented as genuine zero usage. Completed tracked children from failed
launches may also be archived; a history entry does not imply success.

## Signals and job lifecycle

An ordinary pipeline is one job, with one process record per stage.
A background logical chain is one job represented by its supervisor.
The supervisor launches and reaps the chain's pipeline stages.

At the prompt, the shell owns the terminal. A foreground job receives
terminal ownership until it finishes or stops. Children restore default
signal actions before execution.

Signal handlers wake the main loop through a nonblocking self-pipe.
Job-table changes and wait4 calls happen outside signal handlers.

Background child statuses are collected while the shell waits for input.
Completion and stop notifications are displayed at the next clean prompt.

With active jobs, the first exit or EOF request warns.
A repeated exit/EOF request without an intervening command confirms
shutdown. Cleanup sends SIGHUP and SIGCONT, allows a brief grace period,
then escalates to SIGTERM and SIGKILL if necessary and reaps the children.

## Status codes

- 0: success
- 1: general operational failure
- 2: syntax, usage or unsupported-feature error
- 126: command could not be executed
- 127: command not found or empty command name
- 128 + signal number: signal termination
- 130: Ctrl+C input cancellation

A pipeline returns the last stage's status.
Background launch returns 0 when launch setup succeeds; this does not
mean the background program has finished successfully.

## Limits and unsupported features

- Maximum input line: 4096 bytes, excluding the newline
- Maximum arguments per command: 128, including the command name
- Maximum pipeline stages: 16
- Maximum simultaneously registered jobs: 64
- Completed-job statistics history: latest 16 archived entries
- Interactive command history: latest 100 nonblank input lines

Oversized input is discarded rather than partly executed.
NUL bytes are rejected.

The current implementation does not support:
- Tilde expansion or advanced parameter expansion
- Field splitting of expanded values
- Wildcard expansion
- Command substitution
- Subshell/grouping syntax
- Here-documents or arbitrary file-descriptor redirection
- Bare or per-command assignments, loops, functions or a scripting language
- Persistent history, history recall, arrow-key navigation or autocomplete
- A custom memory allocator

Single quotes preserve literal text; double quotes allow variable
expansion. Unquoted semicolons separate list items.
Unquoted parentheses and backticks are still rejected.

After a foreground job completes with a process terminated by SIGINT,
the interactive shell prints a newline before subsequent shell output.
A normal exit with numeric status 130 does not trigger this newline.

## Architecture

| Module | Responsibility |
| --- | --- |
| main.c | Initialization, conditional list execution, status and shutdown |
| input.c | Bounded input and poll-based event waiting |
| history.c | Bounded command storage, numbering, display and cleanup |
| lexer.c | Words, operators, quotes, escapes and original token spelling |
| expansion.c | Quote-aware environment and previous-status expansion |
| parser.c | Command-list validation, token ranges, pipelines and redirections |
| executor.c | Pipes, fork/exec, grouping and launch synchronization |
| chain.c | Background-chain supervisor, evaluation and pipeline lifecycle |
| builtins.c | Parent built-ins and descriptor restoration |
| redirection.c | File opening and standard-stream replacement |
| terminal.c | Terminal ownership, modes and child signal defaults |
| jobs.c | Job states, resource collection, statistics history and cleanup |
| job_control.c | Foreground waiting, resource collection and job resumption |
| signals.c | Minimal signal handlers and self-pipe notifications |

## Automated tests

```bash
make test
```

This builds and runs lexer tests (including four semicolon and ten
logical-operator assertions) and the existing pipeline parser tests, plus:

- 45 command-list parser tests.

- 12 job-state and resource-lifecycle checks.
- 6 fork-isolation checks for inherited job records.
- 8 chain-wait checks, 3 stop/resume checks and 9 token-copy checks.
- 10 chain-pipeline checks and 14 chain-evaluation checks.
- 6 chain-interruption checks.
- 6 supervisor-launch checks plus a parent-continuation assertion.
- 17 expansion unit tests.
- 9 resource-recorder checks.
- 5 actual resource-collection checks.
- 16 completed-job statistics history checks.
- 12 command-history module checks.
- 9 interactive command-history checks using a pseudo-terminal (PTY).
- 60 interactive job-control checks using a pseudo-terminal (PTY).
- 96 non-interactive whole-shell integration tests: 24 general,
  34 command-list, 11 expansion, 10 statistics and 17 environment tests.

Integration tests cover quoting, statuses, pipelines, redirection,
error recovery and configured input/argument/pipeline limits.
Expansion integration tests also cover quote handling, empty values,
pipeline statuses, expanded filenames and built-in arguments.
Statistics integration tests cover output, measurement coverage,
ordering, redirection, argument validation and history limits.
Environment integration tests cover assignment, replacement, child
inheritance, empty values, removal, validation and redirection behavior.
Command-list integration tests cover ordering, expansion timing,
pipelines, redirection, exit behavior and whole-list syntax validation.
They also check conditional execution, left-to-right evaluation,
status preservation and prevention of side effects in skipped items.
Integration tests use temporary directories and clean up their test files.

The history PTY tests check recording, clearing, original spelling,
syntax-error retention, redirection, validation and the 100-entry limit.
They use Python's standard library and require a usable PTY facility.

The job-control PTY tests cover foreground terminal ownership,
Ctrl+C interruption and status 130, Ctrl+Z stopping, bg/fg resumption,
prompt input cancellation, terminal-mode restoration and normal exit.
They also cover background separators, multiple background jobs,
separate job records, continued foreground execution and selecting
each background job with fg.
Logical-chain checks cover whole-chain job records, foreground
handoff, interruption, stop/resume, status 130 and job removal.
They run automatically through make test using Python's standard library.

The manual checks below remain useful for additional scenarios,
including background terminal readers, idle reaping and active-job shutdown.

## Manual job-control checks

- Interrupt sleep and a pipeline with Ctrl+C.
- Stop a pipeline with Ctrl+Z; inspect it with jobs.
- Resume it with bg, then fg.
- Launch cat with &; verify it stops, then accepts input through fg.
- Verify background completion is reaped while the shell is idle.
- Stop a program that disables terminal echo; verify shell echo is
  restored and fg resumes with the program's saved terminal settings.
- Confirm exit warnings and cleanup with a live job.
- Cancel unfinished prompt input with Ctrl+C and verify that the
  cancelled line does not appear in command history.
- Verify stats after background completion and foreground interruption.
- Verify stopped jobs stay out of completed statistics until termination.

## Memory and descriptor checks

```bash
valgrind --leak-check=full --show-leak-kinds=all \
  --track-fds=yes --child-silent-after-fork=yes \
  --error-exitcode=99 ./msh
```

Manual checks on 19 October 2026 found no heap leaks or memory errors,
with only three standard descriptors open at shell exit, for:
- Foreground execution, parsing failures and redirection failures
- Background launch, stop/resume, interruption and shutdown cleanup

The expansion unit tests were also checked under Valgrind:
all 17 passed, all 154 allocations were freed, zero memory errors
were reported, and only three standard descriptors remained open.

With statistics enabled, a full-shell Valgrind check covering empty
history, commands, a pipeline, stats output, redirection and invalid
stats arguments freed all 140 allocations, reported zero memory
errors, and left only three standard descriptors open.

A full-shell environment check covering assignment, replacement,
inheritance, empty values, removal, invalid names and redirection
freed all 383 allocations, reported zero memory errors, and left
only three standard descriptors open.

An interactive history Valgrind check covering recording, display,
clearing, new entries after clearing, redirection and invalid arguments
freed all 194 allocations, reported zero memory errors, and left
only three standard descriptors open.

A command-list Valgrind check covering sequential execution,
status and variable expansion, pipelines, redirection, malformed lists
and early exit freed all 427 allocations, reported zero memory errors,
and left only three standard descriptors open.

A logical-execution Valgrind check covering executed and skipped items,
mixed logical chains, pipelines, environment changes and syntax errors
freed all 510 allocations, reported zero memory errors, and left only
three standard descriptors open.

An interactive background-list Valgrind check covering multiple jobs,
foreground continuation, completion, statistics and malformed input
freed all 204 allocations, reported zero memory errors, and left
only three standard descriptors open.

An interactive background-chain Valgrind run covering successful
completion, short-circuit fallback, foreground continuation and job
removal freed all 304 allocations, reported zero memory errors,
and left only three standard descriptors open. Child diagnostics
were suppressed by --child-silent-after-fork=yes, so this result
describes the parent shell's reported memory and descriptor state.

These results cover the exercised scenarios, not every possible failure.

## Future extensions

The next priority is broader coverage of the base paper's shell behavior,
including shell variables, parameter expansion,
scripts, control structures, grouping and command substitution.

Further extensions such as persistent history, history recall,
autocomplete and a project-specific memory allocator are deferred
until that work is complete.

Each extension should define its behavior, update the relevant module
interfaces and preserve the existing regression tests.
