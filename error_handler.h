#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include "myshell.h"

/* Error handling function declarations */
void print_error(const char *message);
void print_parse_error(const char *message);
int validate_pipeline(pipeline_t *pipeline);
int validate_command(command_t *cmd);

#endif /* ERROR_HANDLER_H */
