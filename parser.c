/**
 * parser.c - Command line parsing implementation
 * 
 * This file handles:
 * 1. Tokenizing input strings into meaningful tokens
 * 2. Parsing tokens into command structures
 * 3. Handling redirections and pipes
 * 4. Building pipeline structures for execution
 */

#include "parser.h"
#include "error_handler.h"
#include <glob.h>

/**
 * Remove leading and trailing whitespace from a string
 * @param str: String to trim (modified in place)
 * @return: Pointer to trimmed string
 */
char *trim_whitespace(char *str) {
    char *end;
    
    /* Trim leading space */
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    /* All spaces? */
    if (*str == 0) {
        return str;
    }
    
    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) {
        end--;
    }
    
    /* Write new null terminator */
    end[1] = '\0';
    
    return str;
}

/**
 * Check if a token is a redirection operator
 * @param token: Token to check
 * @return: 1 if redirection token, 0 otherwise
 */
int is_redirection_token(const char *token) {
    return (strcmp(token, "<") == 0 || 
            strcmp(token, ">") == 0 || 
            strcmp(token, "2>") == 0);
}

/**
 * Count the number of pipe symbols in a command line
 * @param line: Command line to analyze
 * @return: Number of pipes found
 */
int count_pipes(const char *line) {
    int count = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '|') {
            count++;
        }
    }
    return count;
}

/**
 * Initialize a command structure
 * @param cmd: Command structure to initialize
 */
void init_command(command_t *cmd) {
    cmd->args = malloc(MAX_ARGS * sizeof(char*));
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
    
    /* Initialize args array to NULL */
    for (int i = 0; i < MAX_ARGS; i++) {
        cmd->args[i] = NULL;
    }
}

static int command_has_content(const command_t *cmd) {
    return (cmd->argc > 0) ||
           (cmd->input_file != NULL) ||
           (cmd->output_file != NULL) ||
           (cmd->error_file != NULL);
}

/**
 * Add an argument to a command
 * @param cmd: Command to add argument to
 * @param arg: Argument to add (will be duplicated)
 */
void add_argument(command_t *cmd, char *arg) {
    if (cmd->argc < MAX_ARGS - 1) {
        cmd->args[cmd->argc] = strdup(arg);
        cmd->argc++;
        cmd->args[cmd->argc] = NULL; /* Keep NULL-terminated */
    }
}

/**
 * Add an argument to the command, expanding glob patterns when appropriate
 */
static int add_word_argument(command_t *cmd, token_t *token) {
    const char *value = token->value;
    
    if (!token->quoted && strpbrk(value, "*?[") != NULL) {
        glob_t glob_results;
        int glob_flags = GLOB_NOCHECK;
        int glob_ret = glob(value, glob_flags, NULL, &glob_results);
        
        if (glob_ret != 0) {
            print_error("Failed to expand wildcard");
            return -1;
        }
        
        for (size_t i = 0; i < glob_results.gl_pathc; i++) {
            add_argument(cmd, glob_results.gl_pathv[i]);
        }
        
        globfree(&glob_results);
        return 0;
    }
    
    add_argument(cmd, token->value);
    return 0;
}

/**
 * Helper to append a token to the token list
 */
static int push_token(token_t *tokens, int *num_tokens, const char *value,
                      token_type_t type, int quoted) {
    if (*num_tokens >= MAX_ARGS) {
        print_parse_error("Too many tokens in command.");
        return -1;
    }
    
    tokens[*num_tokens].type = type;
    tokens[*num_tokens].value = strdup(value);
    tokens[*num_tokens].quoted = quoted;
    (*num_tokens)++;
    return 0;
}

/**
 * Tokenize a command line into an array of tokens.
 * Handles quotes, escapes, and recognizes special operators.
 * @param line: Input command line
 * @param num_tokens: Output parameter for number of tokens
 * @return: Array of tokens (caller must free) or NULL on error
 */
token_t *tokenize(char *line, int *num_tokens) {
    token_t *tokens = malloc(MAX_ARGS * sizeof(token_t));
    char current_token[MAX_LINE];
    int current_len = 0;
    int in_single_quote = 0;
    int in_double_quote = 0;
    int token_was_quoted = 0;
    
    *num_tokens = 0;
    
    for (size_t i = 0; ; i++) {
        char c = line[i];
        int at_end = (c == '\0');
        
        if (at_end) {
            if (in_single_quote || in_double_quote) {
                print_parse_error("Unmatched quote in command.");
                free_tokens(tokens, *num_tokens);
                return NULL;
            }
            if (current_len > 0) {
                current_token[current_len] = '\0';
                if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                    free_tokens(tokens, *num_tokens);
                    return NULL;
                }
            }
            break;
        }
        
        if (!in_single_quote && c == '\\') {
            char next = line[i + 1];
            if (next != '\0') {
                /* Preserve backslash for common escape sequences so
                   utilities like `echo -e` receive "\\n", "\\t", etc. */
                if (next == 'n' || next == 't' || next == 'r' || next == '\\') {
                    if (current_len < MAX_LINE - 2) {
                        current_token[current_len++] = '\\';
                        current_token[current_len++] = next;
                    }
                } else {
                    current_token[current_len++] = next;
                }
                token_was_quoted = 1;
                i++;
            }
            continue;
        }
        
        if (!in_double_quote && c == '\'' ) {
            in_single_quote = !in_single_quote;
            token_was_quoted = 1;
            continue;
        }
        
        if (!in_single_quote && c == '"') {
            in_double_quote = !in_double_quote;
            token_was_quoted = 1;
            continue;
        }
        
        if (!in_single_quote && !in_double_quote) {
            if (c == ' ' || c == '\t') {
                if (current_len > 0) {
                    current_token[current_len] = '\0';
                    if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                        free_tokens(tokens, *num_tokens);
                        return NULL;
                    }
                    current_len = 0;
                    token_was_quoted = 0;
                }
                continue;
            }
            
            if (c == '2' && line[i + 1] == '>') {
                if (current_len > 0) {
                    current_token[current_len] = '\0';
                    if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                        free_tokens(tokens, *num_tokens);
                        return NULL;
                    }
                    current_len = 0;
                    token_was_quoted = 0;
                }
                if (push_token(tokens, num_tokens, "2>", TOKEN_ERROR_REDIRECT, 0) == -1) {
                    free_tokens(tokens, *num_tokens);
                    return NULL;
                }
                i++;
                continue;
            }
            
            if (c == '|') {
                if (current_len > 0) {
                    current_token[current_len] = '\0';
                    if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                        free_tokens(tokens, *num_tokens);
                        return NULL;
                    }
                    current_len = 0;
                    token_was_quoted = 0;
                }
                if (push_token(tokens, num_tokens, "|", TOKEN_PIPE, 0) == -1) {
                    free_tokens(tokens, *num_tokens);
                    return NULL;
                }
                continue;
            }
            
            if (c == '<') {
                if (current_len > 0) {
                    current_token[current_len] = '\0';
                    if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                        free_tokens(tokens, *num_tokens);
                        return NULL;
                    }
                    current_len = 0;
                    token_was_quoted = 0;
                }
                if (push_token(tokens, num_tokens, "<", TOKEN_INPUT_REDIRECT, 0) == -1) {
                    free_tokens(tokens, *num_tokens);
                    return NULL;
                }
                continue;
            }
            
            if (c == '>') {
                if (current_len > 0) {
                    current_token[current_len] = '\0';
                    if (push_token(tokens, num_tokens, current_token, TOKEN_WORD, token_was_quoted) == -1) {
                        free_tokens(tokens, *num_tokens);
                        return NULL;
                    }
                    current_len = 0;
                    token_was_quoted = 0;
                }
                if (push_token(tokens, num_tokens, ">", TOKEN_OUTPUT_REDIRECT, 0) == -1) {
                    free_tokens(tokens, *num_tokens);
                    return NULL;
                }
                continue;
            }
        }
        
        if (current_len >= MAX_LINE - 1) {
            print_parse_error("Token too long.");
            free_tokens(tokens, *num_tokens);
            return NULL;
        }
        
        current_token[current_len++] = c;
    }
    
    return tokens;
}

/**
 * Free memory allocated for tokens
 * @param tokens: Array of tokens to free
 * @param num_tokens: Number of tokens in array
 */
void free_tokens(token_t *tokens, int num_tokens) {
    for (int i = 0; i < num_tokens; i++) {
        free(tokens[i].value);
    }
    free(tokens);
}

/**
 * Parse tokens into a pipeline structure
 * @param tokens: Array of tokens to parse
 * @param num_tokens: Number of tokens
 * @param pipeline: Output pipeline structure
 * @return: 0 on success, -1 on error
 */
int parse_tokens(token_t *tokens, int num_tokens, pipeline_t *pipeline) {
    if (num_tokens == 0) {
        return 0;
    }
    
    /* Count number of commands (separated by pipes) */
    int num_commands = 1;
    for (int i = 0; i < num_tokens; i++) {
        if (tokens[i].type == TOKEN_PIPE) {
            num_commands++;
        }
    }
    
    /* Allocate memory for commands */
    pipeline->commands = malloc(num_commands * sizeof(command_t));
    pipeline->num_commands = num_commands;
    
    /* Initialize commands */
    for (int i = 0; i < num_commands; i++) {
        init_command(&pipeline->commands[i]);
    }
    
    /* Parse tokens into commands */
    int current_cmd = 0;
    int i = 0;
    
    while (i < num_tokens) {
        if (tokens[i].type == TOKEN_WORD) {
            /* Add argument to current command */
            if (add_word_argument(&pipeline->commands[current_cmd], &tokens[i]) == -1) {
                return -1;
            }
            i++;
        } else if (tokens[i].type == TOKEN_INPUT_REDIRECT) {
            /* Handle input redirection */
            if (i + 1 >= num_tokens || tokens[i + 1].type != TOKEN_WORD) {
                print_parse_error("Input file not specified.");
                return -1;
            }
            pipeline->commands[current_cmd].input_file = strdup(tokens[i + 1].value);
            i += 2;
        } else if (tokens[i].type == TOKEN_OUTPUT_REDIRECT) {
            /* Handle output redirection */
            if (i + 1 >= num_tokens || tokens[i + 1].type != TOKEN_WORD) {
                print_parse_error("Output file not specified.");
                return -1;
            }
            pipeline->commands[current_cmd].output_file = strdup(tokens[i + 1].value);
            i += 2;
        } else if (tokens[i].type == TOKEN_ERROR_REDIRECT) {
            /* Handle error redirection */
            if (i + 1 >= num_tokens || tokens[i + 1].type != TOKEN_WORD) {
                print_parse_error("Error output file not specified.");
                return -1;
            }
            pipeline->commands[current_cmd].error_file = strdup(tokens[i + 1].value);
            i += 2;
        } else if (tokens[i].type == TOKEN_PIPE) {
            /* Move to next command */
            if (i + 1 >= num_tokens) {
                print_parse_error("Command missing after pipe.");
                return -1;
            }
            /* Check for empty command */
            if (!command_has_content(&pipeline->commands[current_cmd])) {
                const char *error_message = "Empty command before pipe.";
                if (i > 0 && tokens[i - 1].type == TOKEN_PIPE) {
                    error_message = "Empty command between pipes.";
                }
                print_parse_error(error_message);
                return -1;
            }
            current_cmd++;
            i++;
        } else {
            i++;
        }
    }

    /* Check if last command is empty */
    if (!command_has_content(&pipeline->commands[current_cmd])) {
        print_parse_error("Empty command at end of pipeline.");
        return -1;
    }
    
    return 0;
}

/**
 * Main parsing function - parses a command line into a pipeline
 * @param line: Input command line (will be modified)
 * @param pipeline: Output pipeline structure
 * @return: 0 on success, -1 on error
 */
int parse_command_line(char *line, pipeline_t *pipeline) {
    /* Make a copy of the line for tokenization */
    char *line_copy = strdup(line);
    int num_tokens;
    
    /* Tokenize the line */
    token_t *tokens = tokenize(line_copy, &num_tokens);
    if (!tokens) {
        free(line_copy);
        return -1;
    }
    
    /* Parse tokens into pipeline */
    int result = parse_tokens(tokens, num_tokens, pipeline);
    
    /* Clean up */
    free_tokens(tokens, num_tokens);
    free(line_copy);
    
    return result;
}

/**
 * Free memory allocated for a pipeline
 * @param pipeline: Pipeline to free
 */
void free_pipeline(pipeline_t *pipeline) {
    if (pipeline->commands) {
        for (int i = 0; i < pipeline->num_commands; i++) {
            command_t *cmd = &pipeline->commands[i];
            
            /* Free arguments */
            if (cmd->args) {
                for (int j = 0; j < cmd->argc; j++) {
                    free(cmd->args[j]);
                }
                free(cmd->args);
            }
            
            /* Free redirection files */
            free(cmd->input_file);
            free(cmd->output_file);
            free(cmd->error_file);
        }
        free(pipeline->commands);
    }
    
    /* Reset pipeline structure */
    pipeline->commands = NULL;
    pipeline->num_commands = 0;
}
