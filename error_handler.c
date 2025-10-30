/**
 * error_handler.c - Error handling and validation implementation
 * 
 * This file handles:
 * 1. Printing consistent error messages
 * 2. Validating parsed commands and pipelines
 * 3. Checking for various error conditions specified in requirements
 */

#include "error_handler.h"

/**
 * Print a system error message with consistent formatting
 * @param message: Error message to print
 */
void print_error(const char *message) {
    fprintf(stderr, "myshell: %s: %s\n", message, strerror(errno));
    fflush(stderr);
}

/**
 * Print a parsing error message
 * @param message: Error message to print
 */
void print_parse_error(const char *message) {
    fprintf(stderr, "myshell: %s\n", message);
    fflush(stderr);
}

/**
 * Validate a single command for common error conditions
 * @param cmd: Command to validate
 * @return: 0 on success, -1 on error
 */
int validate_command(command_t *cmd) {
    /* Check if command has any arguments */
    if (cmd->argc == 0) {
        print_parse_error("Empty command.");
        return -1;
    }
    
    /* Check if command exists (basic check) */
    if (cmd->args[0] == NULL || strlen(cmd->args[0]) == 0) {
        print_parse_error("Invalid command.");
        return -1;
    }
    
    /* Check if input file exists (if specified) */
    if (cmd->input_file) {
        if (access(cmd->input_file, R_OK) != 0) {
            fprintf(stderr, "myshell: %s: %s\n", cmd->input_file, strerror(errno));
            return -1;
        }
    }
    
    return 0;
}

/**
 * Validate a pipeline for error conditions specified in requirements
 * @param pipeline: Pipeline to validate
 * @return: 0 on success, -1 on error
 */
int validate_pipeline(pipeline_t *pipeline) {
    if (!pipeline || pipeline->num_commands == 0) {
        return -1;
    }
    
    /* Validate each command in the pipeline */
    for (int i = 0; i < pipeline->num_commands; i++) {
        if (validate_command(&pipeline->commands[i]) != 0) {
            return -1;
        }
    }
    
    /* Additional validation for pipelines */
    if (pipeline->num_commands > 1) {
        /* Check for input redirection on non-first commands */
        for (int i = 1; i < pipeline->num_commands; i++) {
            if (pipeline->commands[i].input_file) {
                print_parse_error("Input redirection not allowed on commands after pipe.");
                return -1;
            }
        }
        
        /* Check for output redirection on non-last commands */
        for (int i = 0; i < pipeline->num_commands - 1; i++) {
            if (pipeline->commands[i].output_file) {
                print_parse_error("Output redirection not allowed on commands before pipe.");
                return -1;
            }
        }
    }
    
    return 0;
}
