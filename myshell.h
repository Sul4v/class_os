#ifndef MYSHELL_H
#define MYSHELL_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/* Maximum input line length */
#define MAX_LINE 1024
/* Maximum number of arguments */
#define MAX_ARGS 64
/* Maximum number of pipes in a command */
#define MAX_PIPES 32

/* Token types for parsing */
typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_INPUT_REDIRECT,
    TOKEN_OUTPUT_REDIRECT,
    TOKEN_ERROR_REDIRECT,
    TOKEN_EOF
} token_type_t;

/* Token structure */
typedef struct {
    token_type_t type;
    char *value;
    int quoted;
} token_t;

/* Command structure representing a single command with its arguments */
typedef struct {
    char **args;          /* Command arguments (NULL-terminated) */
    int argc;             /* Number of arguments */
    char *input_file;     /* Input redirection file (NULL if none) */
    char *output_file;    /* Output redirection file (NULL if none) */
    char *error_file;     /* Error redirection file (NULL if none) */
} command_t;

/* Pipeline structure representing a series of commands connected by pipes */
typedef struct {
    command_t *commands;  /* Array of commands */
    int num_commands;     /* Number of commands in pipeline */
} pipeline_t;

/* Function declarations from parser.c */
int parse_command_line(char *line, pipeline_t *pipeline);
void free_pipeline(pipeline_t *pipeline);
token_t *tokenize(char *line, int *num_tokens);
void free_tokens(token_t *tokens, int num_tokens);

/* Function declarations from executor.c */
int execute_pipeline(pipeline_t *pipeline);
int execute_single_command(command_t *cmd, int input_fd, int output_fd);
int setup_redirections(command_t *cmd, int *input_fd, int *output_fd, int *error_fd);

/* Function declarations from error_handler.c */
void print_error(const char *message);
void print_parse_error(const char *message);
int validate_pipeline(pipeline_t *pipeline);

#endif /* MYSHELL_H */
