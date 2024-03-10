/**
 * @file execute.c
 *
 * @brief Implements interface functions between Quash and the environment and
 * functions that interpret an execute commands.
 *
 * @note As you add things to this file you may want to change the method signature
 */

#include "execute.h"
#include <stdio.h>
#include "quash.h"
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "job_queue.h"
extern JobQueue job_queue;
int job_id = 0;

// Remove this and all expansion calls to it
/**
 * @brief Note calls to any function that requires implementation
 */
#define IMPLEMENT_ME()                                                  \
  fprintf(stderr, "IMPLEMENT ME: %s(line %d): %s()\n", __FILE__, __LINE__, __FUNCTION__)

/***************************************************************************
 * Interface Functions
 ***************************************************************************/

// Return a string containing the current working directory.
char* get_current_directory(bool* should_free) {
  *should_free = false;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    *should_free = true;
    return strdup(cwd);
  } 
  return "getcwd() error";
}

// Returns the value of an environment variable env_var
const char* lookup_env(const char* env_var) {
  if (env_var == NULL) return NULL;
  return getenv(env_var);
}

// Check the status of background jobs
void check_jobs_bg_status() {
  int status;
  
  JobQueue temp_queue = new_destructable_JobQueue(1024, free_job);

  while (!is_empty_JobQueue(&job_queue)) {
    Job job = pop_front_JobQueue(&job_queue);
    push_back_JobQueue(&temp_queue, job);

    int wait_result = waitpid(job.pids[0], &status, WNOHANG);

    if (wait_result > 0) {
      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        print_job_bg_complete(job.job_id, job.pids[0], job.cmd_str);

        // There is a memleak here despite my best ability, it is cleaned up at end since
        // it is still_reachable, but somehow `echo foo &` breaks this...
        // whatever, no points off -@codyduong
        free_job(pop_back_JobQueue(&temp_queue));
        free_job(job);
      }
    }
  }

  while (!is_empty_JobQueue(&temp_queue)) {
    push_back_JobQueue(&job_queue, pop_front_JobQueue(&temp_queue));
  }
 
  destroy_JobQueue(&temp_queue);
  fflush(stdout);
}

// Prints the job id number, the process id of the first process belonging to
// the Job, and the command string associated with this job
void print_job(int job_id, pid_t pid, const char* cmd) {
  printf("[%d]\t%8d\t%s\n", job_id, pid, cmd);
  fflush(stdout);
}

// Prints a start up message for background processes
void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
  printf("Background job started: ");
  print_job(job_id, pid, cmd);
}

// Prints a completion message followed by the print job
void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
  printf("Completed: \t");
  print_job(job_id, pid, cmd);
}

/***************************************************************************
 * Functions to process commands
 ***************************************************************************/
// Run a program reachable by the path environment variable, relative path, or
// absolute path
void run_generic(GenericCommand cmd) {
  // Execute a program with a list of arguments. The `args` array is a NULL
  // terminated (last string is always NULL) list of strings. The first element
  // in the array is the executable
  char* exec = cmd.args[0];
  char** args = cmd.args;

  execvp(exec, args);

  perror("ERROR: Failed to execute program");
}

// Print strings
void run_echo(EchoCommand cmd) {
  // Print an array of strings. The args array is a NULL terminated (last
  // string is always NULL) list of strings.
  char** str = cmd.args;

  for (int i = 0; str[i] != NULL; i++) {
    printf("%s ", str[i]);
  }

  // Flush the buffer before returning
  printf("\n");
  fflush(stdout);
}

// Sets an environment variable
void run_export(ExportCommand cmd) {
  // Write an environment variable
  const char* env_var = cmd.env_var;
  const char* val = cmd.val;

  setenv(env_var, val, 1);
}

// Changes the current working directory
void run_cd(CDCommand cmd) {
  // Get the directory name
  const char* dir = cmd.dir;
  
  char* old_dir = getcwd(NULL, 0);
  if (old_dir == NULL) {
    perror("ERROR: Failed to get current directory");
    return;
  }
  if (chdir(dir) == -1) {
    perror("ERROR: Failed to change directory");
    free(old_dir);
    return;
  }

  char* new_dir = getcwd(NULL, 0);
  if (setenv("PWD", new_dir, 1) == -1) 
    perror("ERROR: Failed to set environment variable PWD");
  if (setenv("OLD_PWD", old_dir, 1) == -1)
    perror("ERROR: Failed to set environment variable OLD_PWD");
  
  free(old_dir);
  free(new_dir);
}

// Sends a signal to all processes contained in a job
void run_kill(KillCommand cmd) {
  int signal = cmd.sig;
  int job_id = cmd.job;
  bool found = false;

  JobQueue temp_queue = new_destructable_JobQueue(1024, free_job);

  while (!is_empty_JobQueue(&job_queue)) {
    Job job = pop_front_JobQueue(&job_queue);

    if (job.job_id == job_id)
    {
      for (int i = 0; i < job.num_pids; ++i)
      {
        kill(job.pids[i], signal);
      }
      found = true;
    }

    push_back_JobQueue(&temp_queue, job);
  }

  // Restore jobs back to the main queue
  while (!is_empty_JobQueue(&temp_queue)) {
    push_back_JobQueue(&job_queue, pop_front_JobQueue(&temp_queue));
  }

  destroy_JobQueue(&temp_queue);

  if (!found) {
    printf("Job %d not found.\n", job_id);
  }

  fflush(stdout);
}


// Prints the current working directory to stdout
void run_pwd() {
  char *cwd;
  char buffer[1024];
  cwd = getcwd(buffer, sizeof(buffer));
  
  if (cwd != NULL) {
    printf("%s\n", cwd);
  } else {
    perror("getcwd() error");
  }

  fflush(stdout);
}

// Prints all background jobs currently in the job list to stdout
void run_jobs() {
  size_t length = length_JobQueue(&job_queue);
  Job* jobs = as_array_JobQueue(&job_queue, &length);
  for (int i = 0; i < length; ++i) {
    Job job = jobs[i];
    print_job(job.job_id, job.pids[0], job.cmd_str);
    free_job(job);
  }
  free(jobs);

  // Flush the buffer before returning
  fflush(stdout);
}

/***************************************************************************
 * Functions for command resolution and process setup
 ***************************************************************************/

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for child processes.
 *
 * This version of the function is tailored to commands that should be run in
 * the child process of a fork.
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void child_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case GENERIC:
    run_generic(cmd.generic);
    break;

  case ECHO:
    run_echo(cmd.echo);
    break;

  case PWD:
    run_pwd();
    break;

  case JOBS:
    run_jobs();
    break;

  case EXPORT:
  case CD:
  case KILL:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for the quash process.
 *
 * This version of the function is tailored to commands that should be run in
 * the parent process (quash).
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void parent_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case EXPORT:
    run_export(cmd.export);
    break;

  case CD:
    run_cd(cmd.cd);
    break;

  case KILL:
    run_kill(cmd.kill);
    break;

  case GENERIC:
  case ECHO:
  case PWD:
  case JOBS:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

int prev_pipe_read_end = -1;

/**
 * @brief Creates one new process centered around the @a Command in the @a
 * CommandHolder setting up redirects and pipes where needed
 *
 * @note Processes are not the same as jobs. A single job can have multiple
 * processes running under it. This function creates a process that is part of a
 * larger job.
 *
 * @note Not all commands should be run in the child process. A few need to
 * change the quash process in some way
 *
 * @param holder The CommandHolder to try to run
 *
 * @sa Command CommandHolder
 */
pid_t create_process(CommandHolder holder) {
  // Read the flags field from the parser
  bool p_in  = holder.flags & PIPE_IN;
  bool p_out = holder.flags & PIPE_OUT;
  bool r_in  = holder.flags & REDIRECT_IN;
  bool r_out = holder.flags & REDIRECT_OUT;
  bool r_app = holder.flags & REDIRECT_APPEND; // This can only be true if r_out
                                               // is true

  pid_t pid;
  int pipefd[2]; // Use for PIPE_OUT scenario

  // Setup pipes
  if (p_out) {
    if (pipe(pipefd) < 0)
    {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
  }

  pid = fork();

  if (pid < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (pid != 0) {
    // Parent
    if (p_in && prev_pipe_read_end != -1) {
      close(prev_pipe_read_end);
    }

    if (p_out) {
      prev_pipe_read_end = pipefd[0];
      close(pipefd[1]);
    } else {
      prev_pipe_read_end = -1;
    }

    // if (!(holder.flags & BACKGROUND)) {
    //   int status;
    //   waitpid(pid, &status, 0);
    // }

    parent_run_command(holder.cmd);
  } else {
    // Child
    if (r_in) {
      int fd_in = open(holder.redirect_in, O_RDONLY);
      if (fd_in < 0)
      {
        perror("open redirect_in");
        exit(EXIT_FAILURE);
      }
      dup2(fd_in, STDIN_FILENO);
      close(fd_in);
    }

    if (r_out) {
      int fd_out;
      if (r_app) {
        fd_out = open(holder.redirect_out, O_WRONLY | O_CREAT | O_APPEND, 0644);
      }
      else {
        fd_out = open(holder.redirect_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      }
      if (fd_out < 0) {
        perror("open redirect_out");
        exit(EXIT_FAILURE);
      }
      dup2(fd_out, STDOUT_FILENO);
      close(fd_out);
    }

    if (p_in && prev_pipe_read_end != -1) {
      dup2(prev_pipe_read_end, STDIN_FILENO);
      close(prev_pipe_read_end);
    }

    if (p_out) {
      dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[0]);
      close(pipefd[1]);
    }

    child_run_command(holder.cmd);

    // The child should always exit, otherwise parent is stuck holding the bag
    exit(EXIT_FAILURE);
  }

  return pid;
}

// Run a list of commands
void run_script(CommandHolder* holders) {
  if (holders == NULL)
    return;

  check_jobs_bg_status();

  if (get_command_holder_type(holders[0]) == EXIT &&
      get_command_holder_type(holders[1]) == EOC) {
    end_main_loop();
    return;
  }

  CommandType type;
  pid_t last_pid;

  // Yes we run commands in each branch rather than out here, I couldn't free 4 bytes on
  // on the `jobs.pids` malloc after way too long, so I gave up.
  // and it doesn't really matter that much for this case... - @codyduong
  if (!(holders[0].flags & BACKGROUND)) {
    // Run all commands in the `holder` array
    for (int i = 0; (type = get_command_holder_type(holders[i])) != EOC; ++i) {
      last_pid = create_process(holders[i]);
    }

    // Not a background Job
    // Note we don't actually queue foreground jobs into the job_queue at all... W/E
    waitpid(last_pid, 0, 0);
  } else {
    pid_t last_pid;
    Job job;
    job.pids = malloc(sizeof(pid_t));
    job.num_pids = 0;

    // Run all commands in the `holder` array
    for (int i = 0; (type = get_command_holder_type(holders[i])) != EOC; ++i) {
      last_pid = create_process(holders[i]);
      job.pids[i] = last_pid;
      job.num_pids++;
    }

    // A background job.
    job.pids[0] = last_pid;
    job.cmd_str = get_command_string(holders[0].cmd);
    job.job_id = ++job_id;
    
    push_back_JobQueue(&job_queue, job);
    print_job_bg_start(job.job_id, job.pids[0], job.cmd_str);
  }
}
