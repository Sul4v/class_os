/**
 * executor.c - Command execution implementation
 * 
 * This file handles:
 * 1. Executing single commands with fork/exec
 * 2. Setting up input/output/error redirections using dup2()
 * 3. Creating and managing pipes for command pipelines
 * 4. Coordinating execution of complex command pipelines
 */

#include "executor.h"
#include "error_handler.h"

/**
 * Check if a command exists in the PATH or as an executable file
 * @param command: Command name to check
 * @return: 1 if command exists, 0 otherwise
 */
int check_command_exists(const char *command) {
    /* Check if it's an executable file path */
    if (strchr(command, '/') != NULL) {
        return access(command, X_OK) == 0;
    }
    
    /* Check if command exists in PATH */
    char *path = getenv("PATH");
    if (!path) return 0;
    
    char *path_copy = strdup(path);
    char *dir = strtok(path_copy, ":");
    
    while (dir != NULL) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);
        
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return 1;
        }
        
        dir = strtok(NULL, ":");
    }
    
    free(path_copy);
    return 0;
}

/**
 * Create pipes for a pipeline
 * @param pipes: Array to store pipe file descriptors
 * @param num_pipes: Number of pipes to create
 * @return: 0 on success, -1 on error
 */
int create_pipes(int pipes[][2], int num_pipes) {
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipes[i]) == -1) {
            print_error("Failed to create pipe");
            return -1;
        }
    }
    return 0;
}

/**
 * Close all pipe file descriptors
 * @param pipes: Array of pipe file descriptors
 * @param num_pipes: Number of pipes to close
 */
void close_pipes(int pipes[][2], int num_pipes) {
    for (int i = 0; i < num_pipes; i++) {
        close(pipes[i][READ_END]);
        close(pipes[i][WRITE_END]);
    }
}

/**
 * Wait for all child processes to complete
 * @param pids: Array of process IDs
 * @param num_children: Number of children to wait for
 * @return: 0 on success, -1 if any child failed
 */
int wait_for_children(pid_t *pids, int num_children) {
    int status;
    int result = 0;
    
    for (int i = 0; i < num_children; i++) {
        if (waitpid(pids[i], &status, 0) == -1) {
            print_error("Failed to wait for child process");
            result = -1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            /* Child process failed - don't print error as it may be normal */
            result = -1;
        }
    }
    
    return result;
}

/**
 * Setup file redirections for a command
 * @param cmd: Command with redirection information
 * @param input_fd: Input file descriptor to use (or -1 for stdin)
 * @param output_fd: Output file descriptor to use (or -1 for stdout)
 * @param error_fd: Error file descriptor to use (or -1 for stderr)
 * @return: 0 on success, -1 on error
 */
int setup_redirections(command_t *cmd, int *input_fd, int *output_fd, int *error_fd) {
    int fd;
    int opened_input = 0;
    int opened_output = 0;
    int opened_error = 0;
    
    /* Handle input redirection */
    if (cmd->input_file) {
        fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "myshell: %s: %s\n", cmd->input_file, strerror(errno));
            goto error;
        }
        *input_fd = fd;
        opened_input = 1;
    }
    
    /* Handle output redirection */
    if (cmd->output_file) {
        fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fprintf(stderr, "myshell: %s: %s\n", cmd->output_file, strerror(errno));
            goto error;
        }
        *output_fd = fd;
        opened_output = 1;
    }
    
    /* Handle error redirection */
    if (cmd->error_file) {
        fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fprintf(stderr, "myshell: %s: %s\n", cmd->error_file, strerror(errno));
            goto error;
        }
        *error_fd = fd;
        opened_error = 1;
    }
    
    return 0;
    
error:
    if (opened_input && *input_fd != -1) {
        close(*input_fd);
        *input_fd = -1;
    }
    if (opened_output && *output_fd != -1) {
        close(*output_fd);
        *output_fd = -1;
    }
    if (opened_error && *error_fd != -1) {
        close(*error_fd);
        *error_fd = -1;
    }
    return -1;
}

/**
 * Execute a single command with optional input/output redirection
 * @param cmd: Command to execute
 * @param input_fd: Input file descriptor (-1 for stdin)
 * @param output_fd: Output file descriptor (-1 for stdout)
 * @return: 0 on success, -1 on error
 */
int execute_single_command(command_t *cmd, int input_fd, int output_fd) {
    pid_t pid;
    int status;
    int local_input_fd = input_fd;
    int local_output_fd = output_fd;
    int local_error_fd = -1;
    
    /* Do not pre-check command existence here; let execvp fail in the child.
       This ensures any error message follows the command's redirections. */
    
    /* Setup file redirections */
    if (setup_redirections(cmd, &local_input_fd, &local_output_fd, &local_error_fd) == -1) {
        return -1;
    }
    
    /* Fork child process */
    pid = fork();
    
    if (pid == -1) {
        print_error("Failed to fork");
        return -1;
    } else if (pid == 0) {
        /* Child process */
        /* Ensure SIGPIPE has default behavior in children so writers in
           broken pipelines terminate silently instead of printing errors. */
        signal(SIGPIPE, SIG_DFL);
        
        /* Redirect input if needed */
        if (local_input_fd != -1 && local_input_fd != STDIN_FILENO) {
            if (dup2(local_input_fd, STDIN_FILENO) == -1) {
                print_error("Failed to redirect input");
                exit(1);
            }
            close(local_input_fd);
        }
        
        /* Redirect output if needed */
        if (local_output_fd != -1 && local_output_fd != STDOUT_FILENO) {
            if (dup2(local_output_fd, STDOUT_FILENO) == -1) {
                print_error("Failed to redirect output");
                exit(1);
            }
            close(local_output_fd);
        }
        
        /* Redirect error if needed */
        if (local_error_fd != -1 && local_error_fd != STDERR_FILENO) {
            if (dup2(local_error_fd, STDERR_FILENO) == -1) {
                print_error("Failed to redirect stderr");
                exit(1);
            }
            close(local_error_fd);
        }
        
        /* Execute the command */
        execvp(cmd->args[0], cmd->args);
        
        /* If we reach here, execvp failed. For ENOENT on bare commands (no '/'),
           print Bash-like 'command not found'; otherwise show strerror. */
        if (errno == ENOENT && strchr(cmd->args[0], '/') == NULL) {
            fprintf(stderr, "myshell: %s: command not found\n", cmd->args[0]);
        } else {
            fprintf(stderr, "myshell: %s: %s\n", cmd->args[0], strerror(errno));
        }
        exit(1);
    } else {
        /* Parent process */
        
        /* Close file descriptors that were opened for redirection */
        if (local_input_fd != -1 && local_input_fd != input_fd) {
            close(local_input_fd);
        }
        if (local_output_fd != -1 && local_output_fd != output_fd) {
            close(local_output_fd);
        }
        if (local_error_fd != -1) {
            close(local_error_fd);
        }
        
        /* Wait for child to complete */
        if (waitpid(pid, &status, 0) == -1) {
            print_error("Failed to wait for child");
            return -1;
        }
        
        /* Return success if child exited normally with status 0 */
        return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
    }
}

/**
 * Execute a pipeline of commands connected by pipes
 * @param pipeline: Pipeline structure containing commands to execute
 * @return: 0 on success, -1 on error
 */
int execute_pipeline(pipeline_t *pipeline) {
    if (!pipeline || pipeline->num_commands == 0) {
        return -1;
    }
    
    /* Handle single command case */
    if (pipeline->num_commands == 1) {
        return execute_single_command(&pipeline->commands[0], -1, -1);
    }
    
    /* Multiple commands - need pipes */
    int num_pipes = pipeline->num_commands - 1;
    int pipes[MAX_PIPES][2];
    pid_t pids[MAX_PIPES + 1];
    
    /* Create all pipes */
    if (create_pipes(pipes, num_pipes) == -1) {
        return -1;
    }
    
    /* Execute each command in the pipeline */
    for (int i = 0; i < pipeline->num_commands; i++) {
        command_t *cmd = &pipeline->commands[i];
        
        /* Do not pre-check command existence in the parent; allow the child
           to exec and emit errors under the proper redirections. */
        
        /* Fork process for this command */
        pids[i] = fork();
        
        if (pids[i] == -1) {
            print_error("Failed to fork");
            close_pipes(pipes, num_pipes);
            return -1;
        } else if (pids[i] == 0) {
            /* Child process */
            /* Restore default SIGPIPE disposition for pipeline children */
            signal(SIGPIPE, SIG_DFL);
            
            /* Setup input redirection */
            if (i == 0) {
                /* First command - check for input file redirection */
                if (cmd->input_file) {
                    int input_fd = open(cmd->input_file, O_RDONLY);
                    if (input_fd == -1) {
                        fprintf(stderr, "myshell: %s: %s\n", cmd->input_file, strerror(errno));
                        exit(1);
                    }
                    dup2(input_fd, STDIN_FILENO);
                    close(input_fd);
                }
            } else {
                /* Not first command - read from previous pipe */
                dup2(pipes[i-1][READ_END], STDIN_FILENO);
            }
            
            /* Setup output redirection */
            if (i == pipeline->num_commands - 1) {
                /* Last command - check for output file redirection */
                if (cmd->output_file) {
                    int output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (output_fd == -1) {
                        fprintf(stderr, "myshell: %s: %s\n", cmd->output_file, strerror(errno));
                        exit(1);
                    }
                    dup2(output_fd, STDOUT_FILENO);
                    close(output_fd);
                }
            } else {
                /* Not last command - write to next pipe */
                dup2(pipes[i][WRITE_END], STDOUT_FILENO);
            }
            
            /* Setup error redirection */
            if (cmd->error_file) {
                int error_fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (error_fd == -1) {
                    fprintf(stderr, "myshell: %s: %s\n", cmd->error_file, strerror(errno));
                    exit(1);
                }
                dup2(error_fd, STDERR_FILENO);
                close(error_fd);
            }
            
            /* Close all pipe file descriptors */
            close_pipes(pipes, num_pipes);
            
            /* Execute the command */
            execvp(cmd->args[0], cmd->args);
            
            /* If we reach here, execvp failed */
            if (errno == ENOENT && strchr(cmd->args[0], '/') == NULL) {
                fprintf(stderr, "myshell: %s: command not found\n", cmd->args[0]);
            } else {
                fprintf(stderr, "myshell: %s: %s\n", cmd->args[0], strerror(errno));
            }
            exit(1);
        }
    }
    
    /* Parent process - close all pipe file descriptors */
    close_pipes(pipes, num_pipes);
    
    /* Wait for all children to complete */
    int result = wait_for_children(pids, pipeline->num_commands);
    
    return result;
}
