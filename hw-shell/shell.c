#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_wait(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "prints the current working directory to standard output"},
    {cmd_cd, "cd", "changes the current working directory to another directory"},
    {cmd_wait, "wait", "waits until all bg jobs have been terminated"}
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

int cmd_pwd(unused struct tokens* tokens) {
  char buf[4096];
  if (getcwd(buf, 4096) == NULL) {
    printf("getcwd() error\n");
    return 1;
  }
  printf("%s\n", buf);
  return 0;
}

int cmd_cd(struct tokens* tokens) {
  char* desired_directory = tokens_get_token(tokens, 1);
  if (chdir(desired_directory) != 0) {
    printf("chdir() error\n");
    return 1;
  }
  return 0;
}

int cmd_wait(struct tokens* tokens) {
  while (waitpid(-1, NULL, 0) > 0)
  ;
  return 0;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

void sigchld_handler(int sig) {
  while (waitpid(-1, NULL, WNOHANG) > 0)
  ;
}

void setup_sigaction(int signum, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(signum, &sa, NULL) < 0) {
    perror("sigaction error");
    exit(1);
  }
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  setup_sigaction(SIGINT, SIG_IGN);
  setup_sigaction(SIGTSTP, SIG_IGN);
  setup_sigaction(SIGTTOU, SIG_IGN);
  setup_sigaction(SIGCHLD, sigchld_handler);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

typedef struct pipe_fd {
  int fd[2];
} pipe_fd;

int make_pipes(pid_t pids[], int pipes) {
  int processes = pipes + 1;
  pipe_fd fd_arr[pipes];

  for (int i = 0; i < pipes; i++) {
    if (pipe(fd_arr[i].fd) != 0) {
      printf("Error in creating pipe %d\n", i);
      exit(-1);
    }
  }

  int process_id = -1;
  for (int i = 0; i < processes; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      if (i == 0) {
        if (setpgid(getpid(), getpid()) != 0) {
          printf("Error setting process group id\n");
          exit(-1);
        }
      } else {
        if (setpgid(getpid(), pids[0]) != 0) {
          printf("Error setting process group id\n");
          exit(-1);
        }
      }
      signal(SIGINT, SIG_DFL);
      signal(SIGTSTP, SIG_DFL);
      signal(SIGTTOU, SIG_DFL);
      signal(SIGCHLD, SIG_DFL);
      process_id = i;
      break;
    } else {
      pids[i] = pid;
    }
  }

  if (process_id >= 0) {
    if (process_id > 0) {
      dup2(fd_arr[process_id - 1].fd[0], STDIN_FILENO);
    }
    if (process_id < pipes) {
      dup2(fd_arr[process_id].fd[1], STDOUT_FILENO);
    }
  }

  for (int i = 0; i < pipes; i++) {
    close(fd_arr[i].fd[0]);
    close(fd_arr[i].fd[1]);
  }
  return process_id;

}

void run_program(struct tokens* tokens) {
  int token_length = tokens_get_length(tokens);
  bool background = tokens_get_token(tokens, token_length - 1)[0] == '&';
  int pipes = 0;
  for (int i = 0; i < token_length; i++) {
    char* token = tokens_get_token(tokens, i);
    if (token[0] == '|') {
      pipes++;
    }
  }
  int processes = pipes + 1;
  pid_t pids[pipes + 1];
  int process_id = make_pipes(pids, pipes);

  if (process_id >= 0) {
    int seg = 0, seg_start = 0, seg_end = token_length;
    for (int i = 0; i < token_length; i++) {
      if (tokens_get_token(tokens, i)[0] == '|') {
        if (seg == process_id) {
          seg_end = i;
          break;
        }
        seg++;
        seg_start = i + 1;
      }
    }

    int arg_count = seg_end - seg_start;
    char* args[arg_count + 1];
    for (int i = 0; i < arg_count; i++) {
      args[i] = tokens_get_token(tokens, seg_start + i);
    }
    args[arg_count] = NULL;

    int idx = 0;
    while (args[idx] != NULL) {
      if (args[idx][0] == '<') {
        if (args[idx + 1] == NULL) {
          printf("Redirect (<) file name is null\n");
          exit(-1);
        }
        freopen(args[idx + 1], "r", stdin);
        args[idx] = NULL;
        idx++;
      }
      if (args[idx][0] == '>') {
        if (args[idx + 1] == NULL) {
          printf("Redirect (>) file name is null\n");
          exit(-1);
        }
        freopen(args[idx + 1], "w", stdout);
        args[idx] = NULL;
        idx++;
      }
      idx++;
    }

    char* program_name = args[0];
    if (program_name[0] != '/') {
      char* full_path = strndup(getenv("PATH"), 4096);
      char* token;
      char program_full_path[4096];
      char* save;
      token = strtok_r(full_path, ":", &save);
      while (token) {
        strncpy(program_full_path, token, 4096);
        strncat(program_full_path, "/", 2);
        strncat(program_full_path, program_name, 4096);
        execv(program_full_path, args);
        token = strtok_r(NULL, ":", &save);
      }
    } else {
      execv(program_name, args);
    }
    printf("%s can't be found in the path\n", program_name);
    exit(-1);

  } else {
    if (!background) {
      tcsetpgrp(shell_terminal, pids[0]);
      for (int i = 0; i < processes; i++) {
        waitpid(pids[i], NULL, 0);
      }
      tcsetpgrp(shell_terminal, shell_pgid);
    }
  }
}


int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      // fprintf(stdout, "This shell doesn't know how to run programs.\n");
      run_program(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}