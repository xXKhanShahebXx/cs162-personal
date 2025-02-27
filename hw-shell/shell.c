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

typedef struct process_info {
    pid_t pid;
    pid_t pgid;
    bool is_stopped;
    bool is_background;
    char cmd[256];
    struct termios tmodes;
    struct process_info* next;
} process_info_t;

process_info_t* process_list = NULL;
process_info_t* last_process = NULL;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_wait(struct tokens* tokens);
int cmd_fg(struct tokens* tokens);
int cmd_bg(struct tokens* tokens);

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
    {cmd_wait, "wait", "waits until all bg jobs have been terminated"},
    {cmd_fg, "fg", "brings a background process to the foreground"},
    {cmd_bg, "bg", "resumes a stopped background process"}
};

void add_process(pid_t pid, pid_t pgid, bool is_background, const char* cmd) {
    process_info_t* new_process = malloc(sizeof(process_info_t));
    if (!new_process) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    
    new_process->pid = pid;
    new_process->pgid = pgid;
    new_process->is_stopped = false;
    new_process->is_background = is_background;
    strncpy(new_process->cmd, cmd, 255);
    new_process->cmd[255] = '\0';
    
    tcgetattr(shell_terminal, &new_process->tmodes);
    
    new_process->next = NULL;
    
    if (process_list == NULL) {
      process_list = new_process;
    } else {
      last_process->next = new_process;
    }
    last_process = new_process;
}

process_info_t* find_process(pid_t pid) {
    process_info_t* current = process_list;
    while (current != NULL) {
      if (current->pid == pid) {
        return current;
      }
      current = current->next;
    }
    return NULL;
}

void remove_process(pid_t pid) {
    process_info_t* current = process_list;
    process_info_t* previous = NULL;
    
    while (current != NULL) {
      if (current->pid == pid) {
        if (previous == NULL) {
            process_list = current->next;
        } else {
            previous->next = current->next;
        }
        
        if (current == last_process) {
            last_process = previous;
        }
        
        free(current);
        return;
      }
    previous = current;
    current = current->next;
  }
}

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

void cleanup_process_list() {
    process_info_t* current = process_list;
    while (current != NULL) {
      process_info_t* next = current->next;
      free(current);
      current = next;
    }
    process_list = NULL;
    last_process = NULL;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { 
  cleanup_process_list();
  exit(0);
}

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

int cmd_fg(struct tokens* tokens) {
    pid_t pid = 0;
    process_info_t* process = NULL;
    
    if (tokens_get_length(tokens) > 1) {
      pid = atoi(tokens_get_token(tokens, 1));
      process = find_process(pid);
    } else if (last_process != NULL) {
      process = last_process;
      pid = process->pid;
    }
    
    if (process == NULL) {
      fprintf(stderr, "fg: no such job\n");
      return 1;
    }
    
    tcsetpgrp(shell_terminal, process->pgid);
    tcsetattr(shell_terminal, TCSADRAIN, &process->tmodes);
    process->is_background = false;
    
    if (process->is_stopped) {
      process->is_stopped = false;
      kill(-process->pgid, SIGCONT);
    }
    
    int status;
    waitpid(pid, &status, WUNTRACED);
    
    if (WIFSTOPPED(status)) {
      process->is_stopped = true;
      process->is_background = true;
      printf("\n[%d] Stopped\t%s\n", pid, process->cmd);
    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
      remove_process(pid);
    }
    
    tcsetpgrp(shell_terminal, shell_pgid);
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
    return 0;
}

int cmd_bg(struct tokens* tokens) {
    pid_t pid = 0;
    process_info_t* process = NULL;
    if (tokens_get_length(tokens) > 1) {
      pid = atoi(tokens_get_token(tokens, 1));
      process = find_process(pid);
    } else if (last_process != NULL) {
      process = last_process;
      pid = process->pid;
    }
    
    if (process == NULL) {
      fprintf(stderr, "bg: no such job\n");
      return 1;
    }
    
    if (!process->is_stopped) {
      fprintf(stderr, "bg: job already in background\n");
      return 1;
    }
    
    process->is_stopped = false;
    process->is_background = true;
    
    kill(-process->pgid, SIGCONT);
    printf("[%d] %s &\n", pid, process->cmd);
    
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
  pid_t pid;
  int status;
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    process_info_t* process = find_process(pid);
    
    if (process != NULL) {
      if (WIFSTOPPED(status)) {
        process->is_stopped = true;
        printf("\n[%d] Stopped\t%s\n", pid, process->cmd);
      } 
      else if (WIFCONTINUED(status)) {
        process->is_stopped = false;
      }
      else if (WIFEXITED(status) || WIFSIGNALED(status)) {
        if (process->is_background) {
          printf("\n[%d] Done\t%s\n", pid, process->cmd);
        }
        remove_process(pid);
      }
    }
  }
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
  char command[256] = "";
  for (int i = 0; i < tokens_get_length(tokens); i++) {
    if (i > 0) strcat(command, " ");
    strcat(command, tokens_get_token(tokens, i));
  }

  
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
    for (int i = 0; i < processes; i++) {
      add_process(pids[i], pids[0], background, command);
    }

    if (!background) {
      tcsetpgrp(shell_terminal, pids[0]);
      struct termios shell_modes;
      tcgetattr(shell_terminal, &shell_modes);
      
      for (int i = 0; i < processes; i++) {
        int status;
        waitpid(pids[i], &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
          process_info_t* process = find_process(pids[i]);
          if (process) {
            process->is_stopped = true;
            process->is_background = true;
            printf("\n[%d] Stopped\t%s\n", pids[i], command);
          }
        }
      }
      tcsetpgrp(shell_terminal, shell_pgid);
      tcsetattr(shell_terminal, TCSADRAIN, &shell_modes);
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