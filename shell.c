/*
 * Project #1 - Quash (Quite A Shell)
 * Author: Jayvon Myles Cleophat
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_LINE 1024
#define MAX_ARGS 128

extern char **environ;
static pid_t fg_pid = 0;

/* Signal handler for Ctrl-C (SIGINT) */
void handle_sigint(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    } else {
        printf("\n");
        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s> ", cwd);
            fflush(stdout);
        }
    }
}

/* Signal handler for 10-second timeout (SIGALRM) */
void handle_sigalrm(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGKILL);
        printf("\nProcess %d timed out after 10 seconds.\n", fg_pid);
    }
}

/* Parse raw input into tokens and expand $VARIABLES */
int tokenize(char *line, char **args) {
    int count = 0;
    char *token = strtok(line, " \t\r\n");

    while (token != NULL && count < MAX_ARGS - 1) {
        if (token[0] == '$') {
            char *env_val = getenv(token + 1);
            if (env_val != NULL) {
                args[count++] = strdup(env_val);
            } else {
                args[count++] = strdup("");
            }
        } else {
            args[count++] = strdup(token);
        }
        token = strtok(NULL, " \t\r\n");
    }
    args[count] = NULL;
    return count;
}

/* Free allocated argument tokens */
void free_args(char **args, int count) {
    for (int i = 0; i < count; i++) {
        if (args[i]) {
            free(args[i]);
            args[i] = NULL;
        }
    }
}

/* Execute built-in shell commands in parent process */
int execute_builtin(char **args, int argc) {
    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
        free_args(args, argc);
        exit(0);
    }

    if (strcmp(args[0], "cd") == 0) {
        const char *dir = args[1] ? args[1] : getenv("HOME");
        if (!dir || chdir(dir) != 0) {
            perror("cd failed");
        }
        return 1;
    }

    if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd failed");
        }
        return 1;
    }

    if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            printf("%s%s", args[i], (i + 1 < argc) ? " " : "");
        }
        printf("\n");
        return 1;
    }

    if (strcmp(args[0], "env") == 0) {
        if (argc == 1) {
            for (char **env = environ; *env != NULL; env++) {
                printf("%s\n", *env);
            }
        } else {
            char *val = getenv(args[1]);
            if (val) {
                printf("%s\n", val);
            }
        }
        return 1;
    }

    if (strcmp(args[0], "setenv") == 0) {
        if (argc >= 2) {
            char *eq = strchr(args[1], '=');
            if (eq != NULL) {
                *eq = '\0';
                setenv(args[1], eq + 1, 1);
            } else if (argc >= 3) {
                setenv(args[1], args[2], 1);
            }
        }
        return 1;
    }

    return 0;
}

/* Execute a single command with optional I/O redirection */
void execute_single_command(char **args) {
    int in_fd = -1, out_fd = -1;
    int clean_argc = 0;
    char *clean_args[MAX_ARGS];

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] != NULL) {
                in_fd = open(args[++i], O_RDONLY);
                if (in_fd < 0) {
                    perror("Failed to open input file");
                    exit(1);
                }
            }
        } else if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] != NULL) {
                out_fd = open(args[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) {
                    perror("Failed to open output file");
                    exit(1);
                }
            }
        } else {
            clean_args[clean_argc++] = args[i];
        }
    }
    clean_args[clean_argc] = NULL;

    if (in_fd != -1) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd != -1) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    if (clean_argc > 0) {
        execvp(clean_args[0], clean_args);
        perror("execvp() failed");
    }
    exit(1);
}

/* Execute two commands joined by a pipeline (|) */
void execute_piped_command(char **cmd1, char **cmd2, int bg) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe failed");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execute_single_command(cmd1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execute_single_command(cmd2);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    if (!bg) {
        fg_pid = pid2;
        alarm(10);
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
        alarm(0);
        fg_pid = 0;
    }
}

int main(void) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    struct sigaction sa_int, sa_alrm;

    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    sa_alrm.sa_handler = handle_sigalrm;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alrm, NULL);

    while (1) {
        /* Clean up completed background child processes */
        while (waitpid(-1, NULL, WNOHANG) > 0);

        char cwd[MAX_LINE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s> ", cwd);
            fflush(stdout);
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        int argc = tokenize(line, args);
        if (argc == 0) {
            continue;
        }

        /* Check for background execution (&) */
        int bg = 0;
        if (strcmp(args[argc - 1], "&") == 0) {
            bg = 1;
            free(args[argc - 1]);
            args[--argc] = NULL;
        }

        if (argc == 0) {
            continue;
        }

        /* Check for pipeline (|) */
        int pipe_idx = -1;
        for (int i = 0; i < argc; i++) {
            if (strcmp(args[i], "|") == 0) {
                pipe_idx = i;
                break;
            }
        }

        if (pipe_idx != -1) {
            args[pipe_idx] = NULL;
            char **cmd1 = args;
            char **cmd2 = &args[pipe_idx + 1];
            execute_piped_command(cmd1, cmd2, bg);
            free_args(args, argc);
            continue;
        }

        /* Execute built-in command if matched */
        if (execute_builtin(args, argc)) {
            free_args(args, argc);
            continue;
        }

        /* Fork and execute external foreground/background process */
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            execute_single_command(args);
        } else if (pid > 0) {
            if (!bg) {
                fg_pid = pid;
                alarm(10);
                int status;
                waitpid(pid, &status, 0);
                alarm(0);
                fg_pid = 0;
            }
        } else {
            perror("fork failed");
        }

        free_args(args, argc);
    }

    return 0;
}