#ifndef PARSER_H
#define PARSER_H

#include "myshell.h"

/* Parser utility functions */
char *trim_whitespace(char *str);
int is_redirection_token(const char *token);
int count_pipes(const char *line);
void init_command(command_t *cmd);
void add_argument(command_t *cmd, char *arg);

#endif /* PARSER_H */
