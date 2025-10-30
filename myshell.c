/**
 * myshell.c - Main shell program implementation
 * 
 * This file contains the main shell loop that:
 * 1. Displays the shell prompt ($)
 * 2. Reads user input
 * 3. Parses the command line
 * 4. Executes the parsed commands
 * 5. Handles the "exit" command to terminate the shell
 */

#include "myshell.h"

/**
 * Main shell loop function
 * Continuously prompts for input, parses commands, and executes them
 * until the user types "exit"
 */
void shell_loop(void) {
    char line[MAX_LINE];
    pipeline_t pipeline;
    
    while (1) {
        /* Display shell prompt */
        printf("$ ");
        fflush(stdout);
        
        /* Read input line */
        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF encountered (Ctrl+D) */
            printf("\n");
            break;
        }
        
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = '\0';
        
        /* Skip empty lines */
        if (strlen(line) == 0) {
            continue;
        }
        
        /* Check for exit command */
        if (strcmp(line, "exit") == 0) {
            break;
        }
        
        /* Initialize pipeline structure */
        memset(&pipeline, 0, sizeof(pipeline_t));
        
        /* Parse the command line */
        if (parse_command_line(line, &pipeline) == 0) {
            /* Validate the parsed pipeline */
            if (validate_pipeline(&pipeline) == 0) {
                /* Execute the pipeline */
                execute_pipeline(&pipeline);
            }
        }
        
        /* Clean up allocated memory */
        free_pipeline(&pipeline);
    }
}

/**
 * Main function - entry point of the shell program
 * Sets up signal handling and starts the shell loop
 */
int main(void) {
    /* Ignore SIGINT (Ctrl+C) for the shell process itself */
    signal(SIGINT, SIG_IGN);
    
    /* Start the main shell loop */
    shell_loop();
    
    return 0;
}
