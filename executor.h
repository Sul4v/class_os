#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "myshell.h"

/* File descriptor constants */
#define READ_END 0
#define WRITE_END 1

/* Execution utility functions */
int create_pipes(int pipes[][2], int num_pipes);
void close_pipes(int pipes[][2], int num_pipes);
int wait_for_children(pid_t *pids, int num_children);
int check_command_exists(const char *command);

#endif /* EXECUTOR_H */
