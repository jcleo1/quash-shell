# Project 1: Quash (Quite A Shell)
**Author:** Jayvon Myles Cleophat  
**Course:** Operating Systems (CSCI 401) – Howard University  

---

## 1. Project Overview & System Architecture
Quash is a modular, lightweight UNIX command-line interpreter developed in C. It models standard production shells (such as `sh` and `bash`), handling process lifecycle management, process signaling, environment configuration, stream redirection, and inter-process communication pipelines.

The program runs an infinite Read-Eval-Print Loop (REPL):
1. **Prompt Resolution:** Discovers the working directory using `getcwd()` and formats the prompt dynamically as `<current_working_directory>> `.
2. **Lexical Parsing & Tokenization:** Parses raw user inputs on whitespace boundaries (`\t`, `\r`, `\n`, space) into an argument vector (`args[]`) while handling metacharacters (`|`, `<`, `>`, `&`).
3. **Environment Variable Expansion:** Inspects tokens for the leading `$` prefix and resolves their runtime values via `getenv()`.
4. **Command Classification:** Directs input to either internal built-in handlers or external binary process dispatchers.
5. **Process Synchronization & Signal Handling:** Uses `fork()`, `execvp()`, `waitpid()`, and `sigaction()` to govern foreground child synchronization, asynchronous background processes, and interrupt handling.

---

## 2. Component Design & Implementation

### A. Dynamic Tokenization & Variable Substitution
The command line is processed using a custom `tokenize()` function leveraging `strtok()`:
- **Variable Substitution:** When an argument token begins with `$`, the shell queries the environment table via `getenv(token + 1)`. If the variable exists, the token is duplicated into dynamic memory with `strdup()`. If unset, it resolves to an empty string `""`.
- **Memory Management:** Dynamically allocated token vectors are tracked and systematically released using `free_args()` at the end of each REPL cycle to ensure zero memory leakage during sustained interactive sessions.

### B. Shell Built-in Commands
Built-in utilities must execute directly within the shell parent process; executing them within child subshells would cause environment or working directory changes to be lost upon child exit:
- `cd [path]`: Alters the current working directory using `chdir()`. Defaults to `getenv("HOME")` if no destination is provided.
- `pwd`: Fetches and outputs the absolute path of the calling process via `getcwd()`.
- `echo [args...]`: Iterates through argument tokens and prints them to stdout, properly evaluating expanded environment variables.
- `env [var]`: When called without arguments, traverses the global `extern char **environ` pointer array to output all active environment pairs. When passed a variable name, outputs that specific value.
- `setenv [VAR=VAL] / [VAR VAL]`: Parses variable-value pairs and applies them to the shell process table using `setenv(..., 1)` with overwrite enabled.
- `exit` / `quit`: Flushes output buffers, frees dynamically allocated argument arrays, and terminates the main process cleanly via `exit(0)`.

### C. External Process Execution & Background Jobs (`&`)
External programs are executed by creating an isolated process image:
- The shell invokes `fork()`. The child resets default signal dispositions and invokes `execvp()`, loading binaries from paths defined in `PATH`.
- If an execution fails (e.g., command not found), the child prints an error via `perror("execvp() failed")` and exits with an error code.
- **Asynchronous Execution (`&`):** If a command line terminates with an ampersand (`&`), the token is removed and the background flag is set. The parent skips blocking and immediately redisplays the shell prompt.
- **Zombie Harvesting:** To prevent zombie accumulation, non-blocking reaping is performed at the start of each REPL cycle using `waitpid(-1, NULL, WNOHANG)`.

### D. Signal Handling & Execution Safeguards
- **Interactive Interruption (`SIGINT`):** Configured via `sigaction()` with `SA_RESTART`. When the user inputs Ctrl+C, the interrupt signal is redirected specifically to the active foreground child (`fg_pid`). If no child is running, the shell prints a fresh prompt line without terminating.
- **Process Timeout Safeguard (`SIGALRM`):** To avoid deadlocks and hanging commands, an alarm timer is armed via `alarm(10)` prior to foreground blocking. If execution exceeds 10 seconds, `handle_sigalrm()` catches the signal and kills the unresponsive child with `SIGKILL`. If the child terminates normally, `alarm(0)` clears the scheduled interrupt.

### E. I/O Redirection & Pipelines
- **Redirection Operators (`<`, `>`):**
  - `< file`: Opens the file read-only (`O_RDONLY`) and redirects standard input using `dup2(in_fd, STDIN_FILENO)`.
  - `> file`: Opens/creates the target file with truncation (`O_WRONLY | O_CREAT | O_TRUNC, 0644`) and maps standard output using `dup2(out_fd, STDOUT_FILENO)`.
- **Pipeline Implementation (`|`):**
  - Creates a unidirectional pipe via `pipe(pipefd)`.
  - Forks two coordinated children: the upstream process duplicates stdout to `pipefd[1]`, and the downstream process duplicates stdin from `pipefd[0]`.
  - **Descriptor Safety:** Both ends of the pipe are closed in the parent immediately after forking, and unneeded ends are closed within each child to prevent hanging `read()` descriptors waiting on `EOF`.

---

## 3. Trade-offs & Engineering Decisions
- **Dynamic vs. Static Token Storage:** Used dynamic allocation (`strdup`) to decouple parsed arguments from mutable input buffers, preventing side-effects when expanding variables of differing lengths.
- **Process Group Tracking:** Maintained a dedicated static `fg_pid` variable instead of a full job control queue. This ensures asynchronous signal handlers remain clean, reentrant, and fast while satisfying assignment requirements.
- **Pipeline Synchronization:** Used sequential `waitpid()` blocking across both piped children in the parent, ensuring the shell does not display subsequent prompts before downstream output completes.

---

## 4. Verification & Validation
- **Built-in Verification:** Verified path traversal (`cd testDir1`, `cd ..`), directory display (`pwd`), and environment management (`setenv greeting=hello`, `echo $greeting $HOME`, `env`).
- **External Binaries:** Verified utility invocations (`ls -l`, `cp shell.c shell2.c`), error paths (`abc` -> `execvp() failed: No such file or directory`).
- **Redirection & Pipes:** Verified streaming (`ls > out.txt`, `more < shell.c`, `cat testData | grep "this"`).
- **Process Timeouts & Signals:** Verified Ctrl+C interruption on foreground processes and verified 10-second termination using `sleep 15`.
