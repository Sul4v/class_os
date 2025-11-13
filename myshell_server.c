/* Remote myshell server: accepts TCP clients and runs their commands concurrently. */
#include "myshell.h"
#include "network_utils.h"
#include "parser.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 5050
#define BACKLOG 10           /* Pending connection queue depth */
#define SERVER_PORT_ENV "MYSHELL_PORT"

/* All metadata the worker thread needs for logging and cleanup. */
typedef struct {
    int client_fd;
    int client_id;
    int thread_label;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
} client_context_t;

/* Global counters guarded by mutexes so log labels never clash. */
static pthread_mutex_t client_id_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t thread_label_lock = PTHREAD_MUTEX_INITIALIZER;
static int next_client_id = 1;
static int next_thread_label = 1;

/* Parse the CLI/env supplied port while guarding against invalid values. */
static int parse_port(const char *value, int fallback, const char *source) {
    if (!value || *value == '\0') {
        return fallback;
    }

    char *endptr = NULL;
    errno = 0;
    long candidate = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0' ||
        candidate <= 0 || candidate > 65535) {
        if (source) {
            fprintf(stderr,
                    "[WARN] Ignoring invalid %s \"%s\". Using port %d.\n",
                    source, value, fallback);
        }
        return fallback;
    }
    return (int)candidate;
}

static void restore_fd(int original_fd, int target_fd) {
    if (original_fd != -1) {
        dup2(original_fd, target_fd);
        close(original_fd);
    }
}

/* Execute a parsed pipeline while capturing both stdout and stderr into memory. */
static int run_shell_command(const char *command, char **output, int *status) {
    /* Capture child output via a pipe shared between stdout/stderr. */
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    if (stdout_backup == -1 || stderr_backup == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    fflush(stdout);
    fflush(stderr);
    
    if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
        dup2(pipefd[1], STDERR_FILENO) == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        restore_fd(stdout_backup, STDOUT_FILENO);
        restore_fd(stderr_backup, STDERR_FILENO);
        return -1;
    }
    close(pipefd[1]);
    
    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));
    
    int local_status = 0;
    char *line_copy = strdup(command);
    if (!line_copy) {
        restore_fd(stdout_backup, STDOUT_FILENO);
        restore_fd(stderr_backup, STDERR_FILENO);
        close(pipefd[0]);
        return -1;
    }
    
    /* Reuse the Phase-1 parser/executor to keep behavior consistent. */
    if (parse_command_line(line_copy, &pipeline) == 0) {
        if (validate_pipeline(&pipeline) == 0) {
            local_status = (execute_pipeline(&pipeline) == 0) ? 0 : 1;
        } else {
            local_status = 1;
        }
    } else {
        local_status = 1;
    }
    
    free(line_copy);
    free_pipeline(&pipeline);
    
    fflush(stdout);
    fflush(stderr);
    restore_fd(stdout_backup, STDOUT_FILENO);
    restore_fd(stderr_backup, STDERR_FILENO);
    
    char buffer[512];
    size_t capacity = 1024;
    size_t total = 0;
    char *result = malloc(capacity);
    if (!result) {
        close(pipefd[0]);
        return -1;
    }
    
    ssize_t bytes;
    /* Slurp everything the child wrote into a dynamically sized buffer. */
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (total + (size_t)bytes + 1 > capacity) {
            capacity = (capacity + bytes) * 2;
            char *resized = realloc(result, capacity);
            if (!resized) {
                free(result);
                close(pipefd[0]);
                return -1;
            }
            result = resized;
        }
        memcpy(result + total, buffer, (size_t)bytes);
        total += (size_t)bytes;
    }
    close(pipefd[0]);
    
    if (bytes == -1) {
        free(result);
        return -1;
    }
    
    result[total] = '\0';
    *output = result;
    if (status) {
        *status = local_status;
    }
    
    return 0;
}

/* Helper that mirrors the rubric logging style for multi-line responses. */
static void log_multiline_output(const char *prefix, const char *output) {
    if (!output || output[0] == '\0') {
        printf("%s (no output)\n", prefix);
        return;
    }
    
    printf("%s\n", prefix);
    fputs(output, stdout);
    if (output[strlen(output) - 1] != '\n') {
        putchar('\n');
    }
}

static int extract_missing_command(const char *output, char **missing) {
    /* Be robust to both messages:
       - "myshell: <cmd>: command not found"
       - "myshell: <cmd>: No such file or directory"
       Scan line-by-line for a prefix and accepted suffixes. */
    const char *prefix = "myshell: ";
    const char *suffix1 = "command not found";
    const char *suffix2 = "No such file or directory";
    size_t prefix_len = strlen(prefix);

    if (!output) {
        return 0;
    }

    const char *p = output;
    while (*p != '\0') {
        /* Move to start of next non-empty line */
        while (*p == '\n') p++;
        if (*p == '\0') break;

        if (strncmp(p, prefix, prefix_len) == 0) {
            const char *cmd_start = p + prefix_len;
            const char *colon = strchr(cmd_start, ':');
            if (colon) {
                const char *msg = colon + 1;
                while (*msg == ' ') msg++;
                if ((strstr(msg, suffix1) != NULL) || (strstr(msg, suffix2) != NULL)) {
                    size_t cmd_len = (size_t)(colon - cmd_start);
                    char *command = malloc(cmd_len + 1);
                    if (!command) {
                        return 0;
                    }
                    memcpy(command, cmd_start, cmd_len);
                    command[cmd_len] = '\0';
                    *missing = command;
                    return 1;
                }
            }
        }

        /* Advance to next line */
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }

    return 0;
}

/* Verify that a candidate token is actually one of the command names
   (argv[0]) present in the parsed pipeline of the provided command line. */
static int is_actual_command_name(const char *command_line, const char *candidate) {
    if (!command_line || !candidate || *candidate == '\0') {
        return 0;
    }
    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));
    char *copy = strdup(command_line);
    if (!copy) {
        return 0;
    }
    int result = 0;
    if (parse_command_line(copy, &pipeline) == 0 && pipeline.num_commands > 0) {
        for (int i = 0; i < pipeline.num_commands; i++) {
            command_t *cmd = &pipeline.commands[i];
            if (cmd->argc > 0 && cmd->args[0] && strcmp(cmd->args[0], candidate) == 0) {
                result = 1;
                break;
            }
        }
    }
    free(copy);
    free_pipeline(&pipeline);
    return result;
}

/* Per-client service loop: read a line, execute it, log everything, send response. */
static void handle_client(client_context_t *ctx) {
    while (1) {
        char *command = NULL;
        size_t command_length = 0;
        
        if (recv_message(ctx->client_fd, &command, &command_length) == -1) {
            printf("[INFO] [Client #%d - %s:%d] Disconnected unexpectedly.\n",
                   ctx->client_id, ctx->client_ip, ctx->client_port);
            free(command);
            break;
        }
        
        char *trimmed = trim_whitespace(command);
        if (trimmed[0] == '\0') {
            /* Client hit enter on an empty line—send a blank reply so the prompt returns. */
            free(command);
            printf("[OUTPUT] [Client #%d - %s:%d] Empty command received; sending acknowledgment.\n",
                   ctx->client_id, ctx->client_ip, ctx->client_port);
            if (send_message(ctx->client_fd, "", 0) == -1) {
                printf("[ERROR] [Client #%d - %s:%d] Failed to acknowledge empty command.\n",
                       ctx->client_id, ctx->client_ip, ctx->client_port);
                break;
            }
            continue;
        }
        
        printf("\n");
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n",
               ctx->client_id, ctx->client_ip, ctx->client_port, trimmed);
        
        if (strcmp(trimmed, "exit") == 0) {
            /* Client asked to close the session; fall out of the loop gracefully. */
            free(command);
            printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. Closing connection.\n",
                   ctx->client_id, ctx->client_ip, ctx->client_port);
            break;
        }
        
        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n",
               ctx->client_id, ctx->client_ip, ctx->client_port, trimmed);
        
        char *output = NULL;
        int status = 0;
        if (run_shell_command(trimmed, &output, &status) == -1) {
            /* Phase-1 executor reported an internal failure; apologize to this client only. */
            const char *error_message = "Internal server error.\n";
            printf("[ERROR] [Client #%d - %s:%d] Failed to execute command.\n",
                   ctx->client_id, ctx->client_ip, ctx->client_port);
            char prefix[256];
            snprintf(prefix, sizeof(prefix),
                     "[OUTPUT] [Client #%d - %s:%d] Sending error message to client:",
                     ctx->client_id, ctx->client_ip, ctx->client_port);
            log_multiline_output(prefix, error_message);
            if (send_message(ctx->client_fd, error_message, strlen(error_message)) == -1) {
                printf("[ERROR] [Client #%d - %s:%d] Failed to send internal error message.\n",
                       ctx->client_id, ctx->client_ip, ctx->client_port);
                free(command);
                free(output);
                break;
            }
            free(command);
            free(output);
            continue;
        }
        
        char prefix[256];
        if (status == 0) {
            snprintf(prefix, sizeof(prefix),
                     "[OUTPUT] [Client #%d - %s:%d] Sending output to client:",
                     ctx->client_id, ctx->client_ip, ctx->client_port);
            log_multiline_output(prefix, output);
        } else {
            char *missing_cmd = NULL;
            if (extract_missing_command(output, &missing_cmd) &&
                is_actual_command_name(trimmed, missing_cmd)) {
                /* Log rubric-formatted line, but send the actual captured output
                   so client sees both the error and any pipeline output (e.g., '0'). */
                printf("[ERROR] [Client #%d - %s:%d] Command not found: \"%s\"\n",
                       ctx->client_id, ctx->client_ip, ctx->client_port, missing_cmd);
                snprintf(prefix, sizeof(prefix),
                         "[OUTPUT] [Client #%d - %s:%d] Sending error message to client:",
                         ctx->client_id, ctx->client_ip, ctx->client_port);
                log_multiline_output(prefix, output);
                free(missing_cmd);
            } else {
                printf("[ERROR] [Client #%d - %s:%d] Command completed with errors.\n",
                       ctx->client_id, ctx->client_ip, ctx->client_port);
                snprintf(prefix, sizeof(prefix),
                         "[OUTPUT] [Client #%d - %s:%d] Sending error message to client:",
                         ctx->client_id, ctx->client_ip, ctx->client_port);
                log_multiline_output(prefix, output);
            }
        }
        
        size_t send_length = strlen(output);
        if (send_message(ctx->client_fd, output, send_length) == -1) {
            /* If the socket errored here, assume the client vanished. */
            printf("[ERROR] [Client #%d - %s:%d] Failed to send response to client.\n",
                   ctx->client_id, ctx->client_ip, ctx->client_port);
            free(command);
            free(output);
            break;
        }
        
        free(command);
        free(output);
    }
}

/* Thread trampoline that owns a client until disconnect, then frees resources. */
static void *client_thread(void *arg) {
    client_context_t *ctx = arg;
    handle_client(ctx);
    printf("[INFO] Client #%d disconnected from %s:%d.\n",
           ctx->client_id, ctx->client_ip, ctx->client_port);
    close(ctx->client_fd);
    free(ctx);
    return NULL;
}

/* Listener loop: accept sockets, label them, and hand them to detached threads. */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    port = parse_port(getenv(SERVER_PORT_ENV), port, SERVER_PORT_ENV);
    if (argc > 1) {
        port = parse_port(argv[1], port, "command-line port");
    }
    
    signal(SIGPIPE, SIG_IGN);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    printf("[INFO] Server started, waiting for client connections on port %d...\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        client_context_t *ctx = malloc(sizeof(*ctx));
        if (!ctx) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        ctx->client_fd = client_fd;
        ctx->client_port = ntohs(client_addr.sin_port);
        if (!inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip))) {
            strncpy(ctx->client_ip, "unknown", sizeof(ctx->client_ip) - 1);
            ctx->client_ip[sizeof(ctx->client_ip) - 1] = '\0';
        }
        
        pthread_mutex_lock(&client_id_lock);
        ctx->client_id = next_client_id++;
        pthread_mutex_unlock(&client_id_lock);
        
        pthread_mutex_lock(&thread_label_lock);
        ctx->thread_label = next_thread_label++;
        pthread_mutex_unlock(&thread_label_lock);
        
        printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
               ctx->client_id, ctx->client_ip, ctx->client_port, ctx->thread_label);
        
        pthread_t thread_handle;
        if (pthread_create(&thread_handle, NULL, client_thread, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ctx);
            continue;
        }
        
        if (pthread_detach(thread_handle) != 0) {
            perror("pthread_detach");
        }
    }
    
    close(server_fd);
    return EXIT_SUCCESS;
}
