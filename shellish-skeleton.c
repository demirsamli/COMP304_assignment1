#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>

const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

/*
  We need a helper function to resolve command name to full path.
  We are going to do that by searching for PATH.
  If the command contains '/', then it'll be treated as a path and will be used directly.
  If the path isn't found, it returns NULL; otherwise, it returns the full path.
  At the end, the returned string is freed.
*/

char *res_cmd_path(const char *command){
  // 1) We start by declaring variables ('char *' to imply a sequence of characters in C) that will be used later on.
  char *path_copy;
  char *dir;
  char *full_path;
  size_t path_len;

  // 2) If command has '/', treat it as a path.
  // We can make sure of this since in Unix, '/' is the directory separator. It can't be present inside a filename.
  // For loop to look for '/' in command.
  int has_slash = 0;
  for (int i = 0; command[i] != '\0'; i++) {
    if (command[i] == '/') {
      has_slash = 1;
      break;
    }
  }
  // If '/' present, we treat it as a path.
  if (has_slash) {
    // Returns 0 if the file exists & we have execute permission, -1 otherwise.
    if (access(command, X_OK) == 0){
      // If the file exists and we have execute permission; allocate a new block of memory, copy the string 'command' to it
      // and return a pointer to that copy. => This can easily done via the 'strdup' function.
      return strdup(command);
    }
    // In case where access returns -1, we return NULL.
    return NULL;
  }
    
  
  // In case has_slash == -1; we get the path and work with it using 'copy_path' to avoid confusions.
  // We also do NULL checks to make sure.
  char *path_env = getenv("PATH");
  if (path_env == NULL)
    return NULL;

  path_copy = strdup(path_env);
  if (path_copy == NULL)
    return NULL;

  // After initializing 'copy_path' efficiently, we divide PATH into directories since it's a list of directories itself
  // We do this to find where the actual command lies.
  // PATH can be separated to it's directories by 'strtok', using colon as the separation factor.
  dir = strtok(path_copy, ":");
  // We try out each directory to find the one we are looking for.
  while (dir != NULL){
    // We have to learn the length in order to reserve the right amount of memory for 'full_path' that is going to be created.
    // The number of bytes we need is length of 'dir' + 1 for '/' + length of 'command' + 1 for the null terminator '\0'.
    path_len = strlen(dir) + 1 + strlen(command) + 1;
    // Now, we need to allocate as much memory as path_len by 'malloc'.
    full_path = (char *)malloc(path_len);
    // If 'malloc' returns NULL (it can't allocate the needed memory), we need to free the allocated memory for 'path_copy'.
    // That's done in order to prevent memory leak.
    if (full_path == NULL) {
      free(path_copy);
      return NULL;
    }
    // In case where we successfully allocated the memory we need for the 'full_path', we have to actually place it.
    // This process could be done with 'snprintf' function where it receives the pointer to the buffer, the max number of
    // bytes, and the format (in this case "%s/%s", 'dir' to replace the first 's', and command to replace the second 's')
    snprintf(full_path, path_len, "%s/%s", dir, command);
    // At this point, the allocated memory for 'full_path' actually holds the string for full path.
    // Now, we have to check 'access' as we did before.
    if (access(full_path, X_OK) == 0) {
      free(path_copy);
      return full_path;
    }
    // We free the memory to prevent memory leaks.
    free(full_path);
    dir = strtok(NULL, ":");

  }
  // We free the memory to prevent memory leaks.
  free(path_copy);
  return NULL;
}

/* Apply I/O redirection in the child: redirects[0]=stdin, [1]=stdout overwrite, [2]=stdout append */
static void apply_redirects(struct command_t *command) {
  extern int open(const char *pathname, int flags, ...);
  int shell_o_rdonly = 0, shell_o_wronly = 1, shell_o_creat = 64, shell_o_trunc = 512, shell_o_append = 1024;
  int fd;
  if (command->redirects[0] != NULL) {
    fd = open(command->redirects[0], shell_o_rdonly);
    if (fd == -1) {
      fprintf(stderr, "-%s: %s: %s\n", sysname, command->redirects[0], strerror(errno));
      exit(1);
    }
    dup2(fd, STDIN_FILENO);
    close(fd);
  }
  if (command->redirects[1] != NULL) {
    fd = open(command->redirects[1], shell_o_wronly | shell_o_creat | shell_o_trunc, 0644);
    if (fd == -1) {
      fprintf(stderr, "-%s: %s: %s\n", sysname, command->redirects[1], strerror(errno));
      exit(1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
  }
  if (command->redirects[2] != NULL) {
    fd = open(command->redirects[2], shell_o_wronly | shell_o_creat | shell_o_append, 0644);
    if (fd == -1) {
      fprintf(stderr, "-%s: %s: %s\n", sysname, command->redirects[2], strerror(errno));
      exit(1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
  }
}

static void run_exec(struct command_t *command);

/* Last command in pipeline: run one process with optional stdin and redirects */
static void run_pipeline_with_stdin(struct command_t *command, int stdin_fd, int wait_for_children) {
  if (command == NULL)
    return;
  if (command->next == NULL) {
    pid_t pid = fork();
    if (pid == 0) {
      if (stdin_fd >= 0) {
        dup2(stdin_fd, STDIN_FILENO);
        close(stdin_fd);
      }
      apply_redirects(command);
      run_exec(command);
      exit(127);
    }
    if (stdin_fd >= 0)
      close(stdin_fd);
    if (wait_for_children)
      waitpid(pid, NULL, 0);
    return;
  }
  int pipe_fd[2];
  if (pipe(pipe_fd) == -1) {
    perror("pipe");
    if (stdin_fd >= 0)
      close(stdin_fd);
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    if (stdin_fd >= 0) {
      dup2(stdin_fd, STDIN_FILENO);
      close(stdin_fd);
    }
    dup2(pipe_fd[1], STDOUT_FILENO);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    apply_redirects(command);
    run_exec(command);
    exit(127);
  }
  if (stdin_fd >= 0)
    close(stdin_fd);
  close(pipe_fd[1]);
  run_pipeline_with_stdin(command->next, pipe_fd[0], wait_for_children);
  close(pipe_fd[0]);
  if (wait_for_children)
    waitpid(pid, NULL, 0);
}

static void run_exec(struct command_t *command) {
  char *full_path = res_cmd_path(command->name);
  if (full_path != NULL) {
    execv(full_path, command->args);
    free(full_path);
  }
  fprintf(stderr, "-%s: %s: command not found\n", sysname, command->name);
  exit(127);
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  // Part 2: pipeline (cmd1 | cmd2 | ...) — run it and return
  if (command->next != NULL) {
    run_pipeline_with_stdin(command, -1, !command->background);
    return SUCCESS;
  }

  pid_t pid = fork();
  if (pid == 0) // child
  {
    apply_redirects(command);  // Part 2: <, >, >>

    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    char *full_path = res_cmd_path(command->name);
    if (full_path != NULL) {
      // Replace the current process with a new program
      execv(full_path, command->args);
      free(full_path);
    }
    // do so by replacing the execvp call below
    // execvp(command->name, command->args); // exec+args+path - replaced line (commented out)
    printf("-%s: %s: command not found\n", sysname, command->name);
    exit(127);
  } else {
    // TODO: implement background processes here
    // Parent only waits when the command isn't in the background.
    if (!command->background){
      wait(0); // wait for child process to finish
    }
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
