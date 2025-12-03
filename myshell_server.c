/* Remote myshell server: accepts TCP clients and runs their commands concurrently. */
#include "myshell.h"
#include "network_utils.h"
#include "parser.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT 5050
#define BACKLOG 10           /* Pending connection queue depth */
#define SERVER_PORT_ENV "MYSHELL_PORT"

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[92m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_BLUE    "\033[94m"
#define COLOR_CYAN    "\033[96m"
#define COLOR_WHITE   "\033[97m"
#define COLOR_BG_CYAN "\033[46m"

/* All metadata the worker thread needs for logging and cleanup. */
typedef struct {
    int client_fd;
    int client_id;
    int thread_label;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int disconnected;
    pthread_mutex_t send_lock;
} client_context_t;

typedef enum {
    TASK_TYPE_SHELL,
    TASK_TYPE_PROGRAM
} task_type_t;

typedef struct task {
    int id;
    client_context_t *client;
    char *command;
    task_type_t type;
    double burst_time;
    double remaining_time;
    int first_round_completed;
    int has_started;
    int is_running;
    pid_t pid;
    pid_t pgid;
    int output_fd;
    char *output_buffer;
    size_t output_len;
    size_t output_cap;
    size_t sent_len;
    int streaming;
    int exit_code;
    int completed;
    int cancelled;
    int preempt_requested;
    int arrival_order;
    struct task *next;
} task_t;

static const double FIRST_ROUND_QUANTUM = 3.0;
static const double LATER_ROUND_QUANTUM = 7.0;
static const double DEFAULT_PROGRAM_BURST = 8.0;

static pthread_mutex_t scheduler_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scheduler_cond = PTHREAD_COND_INITIALIZER;
static task_t *ready_head = NULL;
static task_t *ready_tail = NULL;
static task_t *current_task = NULL;
static int next_task_id = 1;
static int next_arrival_order = 1;
static int last_scheduled_task = -1;
static int scheduler_shutdown = 0;
static pthread_t scheduler_thread;

/* Gantt chart tracking */
#define MAX_GANTT_ENTRIES 256
static struct {
    int task_id;
    int time;
} gantt_history[MAX_GANTT_ENTRIES];
static int gantt_count = 0;
static int gantt_time = 0;
static pthread_mutex_t gantt_lock = PTHREAD_MUTEX_INITIALIZER;

static void gantt_record_stop(int task_id, int elapsed) {
    pthread_mutex_lock(&gantt_lock);
    gantt_time += elapsed;
    if (gantt_count < MAX_GANTT_ENTRIES) {
        gantt_history[gantt_count].task_id = task_id;
        gantt_history[gantt_count].time = gantt_time;
        gantt_count++;
    }
    pthread_mutex_unlock(&gantt_lock);
}

static void gantt_print_and_reset(void) {
    pthread_mutex_lock(&gantt_lock);
    if (gantt_count > 0) {
        printf(COLOR_BG_CYAN "P%d-(%d)", gantt_history[0].task_id, gantt_history[0].time);
        for (int i = 1; i < gantt_count; i++) {
            printf("-P%d-(%d)", gantt_history[i].task_id, gantt_history[i].time);
        }
        printf(COLOR_RESET "\n");
        gantt_count = 0;
        gantt_time = 0;
    }
    pthread_mutex_unlock(&gantt_lock);
}

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

static void execute_command_in_child(const char *command) {
    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));

    int exit_code = 0;
    char *line_copy = strdup(command);
    if (!line_copy) {
        _exit(1);
    }

    if (parse_command_line(line_copy, &pipeline) == 0) {
        if (validate_pipeline(&pipeline) == 0) {
            exit_code = (execute_pipeline(&pipeline) == 0) ? 0 : 1;
        } else {
            exit_code = 1;
        }
    } else {
        exit_code = 1;
    }

    free(line_copy);
    free_pipeline(&pipeline);
    fflush(stdout);
    fflush(stderr);
    _exit(exit_code);
}

static pid_t spawn_backend_process(const char *command, int start_suspended, int *out_fd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        /* Redirect stdin from /dev/null to prevent blocking on input */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
            dup2(pipefd[1], STDERR_FILENO) == -1) {
            _exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (start_suspended) {
            raise(SIGSTOP);
        }
        execute_command_in_child(command);
    }

    setpgid(pid, pid);
    close(pipefd[1]);

    if (start_suspended) {
        int st;
        if (waitpid(pid, &st, WUNTRACED) == -1) {
            close(pipefd[0]);
            kill(pid, SIGKILL);
            return -1;
        }
    }

    *out_fd = pipefd[0];
    return pid;
}

/* Execute a parsed pipeline and capture its output synchronously. */
static int run_shell_command(const char *command, char **output, int *status) {
    int read_fd = -1;
    pid_t pid = spawn_backend_process(command, 0, &read_fd);
    if (pid == -1) {
        return -1;
    }

    size_t capacity = 1024;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) {
        close(read_fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    char chunk[512];
    ssize_t bytes;
    while ((bytes = read(read_fd, chunk, sizeof(chunk))) > 0) {
        if (length + (size_t)bytes + 1 > capacity) {
            capacity = (capacity + (size_t)bytes) * 2;
            char *resized = realloc(buffer, capacity);
            if (!resized) {
                free(buffer);
                close(read_fd);
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                return -1;
            }
            buffer = resized;
        }
        memcpy(buffer + length, chunk, (size_t)bytes);
        length += (size_t)bytes;
    }
    close(read_fd);

    if (bytes == -1) {
        free(buffer);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    int wstatus;
    if (waitpid(pid, &wstatus, 0) == -1) {
        free(buffer);
        return -1;
    }

    buffer[length] = '\0';
    *output = buffer;
    if (status) {
        *status = (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) ? 0 : 1;
    }
    return 0;
}

static int send_locked_message(client_context_t *ctx, const char *data, size_t length) {
    if (!ctx || ctx->disconnected) {
        return -1;
    }
    pthread_mutex_lock(&ctx->send_lock);
    int rc = send_message(ctx->client_fd, data, length);
    pthread_mutex_unlock(&ctx->send_lock);
    if (rc == -1) {
        ctx->disconnected = 1;
    }
    return rc;
}

static void send_completion_signal(task_t *task) {
    if (!task || !task->client || task->client->disconnected) {
        return;
    }
    send_locked_message(task->client, "", 0);
}

static void append_output(task_t *task, const char *data, size_t length) {
    if (!task) return;
    size_t needed = task->output_len + length + 1;
    if (needed > task->output_cap) {
        size_t new_cap = task->output_cap == 0 ? 1024 : task->output_cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *resized = realloc(task->output_buffer, new_cap);
        if (!resized) {
            return;
        }
        task->output_buffer = resized;
        task->output_cap = new_cap;
    }
    memcpy(task->output_buffer + task->output_len, data, length);
    task->output_len += length;
    task->output_buffer[task->output_len] = '\0';
}

static void flush_stream_output(task_t *task) {
    if (!task || !task->streaming || !task->client || task->client->disconnected) {
        return;
    }
    if (task->output_len <= task->sent_len) {
        return;
    }
    size_t new_len = task->output_len - task->sent_len;
    const char *payload = task->output_buffer + task->sent_len;
    if (send_locked_message(task->client, payload, new_len) == -1) {
        task->cancelled = 1;
        return;
    }
    task->sent_len = task->output_len;
}

static void drain_task_pipe(task_t *task) {
    if (!task || task->output_fd < 0) {
        return;
    }
    char chunk[512];
    ssize_t bytes;
    while ((bytes = read(task->output_fd, chunk, sizeof(chunk))) > 0) {
        append_output(task, chunk, (size_t)bytes);
        flush_stream_output(task);
    }
    if (bytes == 0) {
        close(task->output_fd);
        task->output_fd = -1;
    } else if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* Nothing to read now. */
    } else if (bytes == -1) {
        close(task->output_fd);
        task->output_fd = -1;
    }
}

static void free_task(task_t *task) {
    if (!task) return;
    free(task->command);
    free(task->output_buffer);
    if (task->output_fd != -1) {
        close(task->output_fd);
    }
    if (task->pid > 0 && !task->completed) {
        killpg(task->pgid > 0 ? task->pgid : task->pid, SIGKILL);
        waitpid(task->pid, NULL, 0);
    }
    free(task);
}


static int parse_program_command(const char *line, char **actual_command, double *burst_hint) {
    const char *ptr = line;
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    if (strncmp(ptr, "program", 7) == 0 && isspace((unsigned char)ptr[7])) {
        ptr += 7;
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        char *endptr = NULL;
        double parsed = strtod(ptr, &endptr);
        if (ptr == endptr) {
            parsed = DEFAULT_PROGRAM_BURST;
        }
        ptr = endptr;
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if (*ptr == '\0') {
            return 0;
        }
        *actual_command = strdup(ptr);
        if (!*actual_command) {
            return 0;
        }
        *burst_hint = parsed > 0 ? parsed : DEFAULT_PROGRAM_BURST;
        return 1;
    }

    const char *token_start = ptr;
    while (*ptr && !isspace((unsigned char)*ptr)) {
        ptr++;
    }
    size_t token_len = (size_t)(ptr - token_start);
    if (token_len == 0) {
        return 0;
    }
    char command_token[64];
    if (token_len >= sizeof(command_token)) {
        token_len = sizeof(command_token) - 1;
    }
    memcpy(command_token, token_start, token_len);
    command_token[token_len] = '\0';

    if (strcmp(command_token, "demo") == 0 || strcmp(command_token, "./demo") == 0) {
        while (*ptr && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if (*ptr == '\0') {
            return 0;
        }
        char *endptr = NULL;
        long parsed = strtol(ptr, &endptr, 10);
        if (ptr == endptr) {
            return 0;
        }
        *actual_command = strdup(line);
        if (!*actual_command) {
            return 0;
        }
        *burst_hint = parsed > 0 ? (double)parsed : DEFAULT_PROGRAM_BURST;
        return 1;
    }

    return 0;
}

static task_t *create_task_from_command(client_context_t *ctx, const char *line) {
    char *stored_command = NULL;
    double burst_hint = DEFAULT_PROGRAM_BURST;
    task_type_t type = TASK_TYPE_SHELL;

    if (parse_program_command(line, &stored_command, &burst_hint)) {
        type = TASK_TYPE_PROGRAM;
    } else {
        stored_command = strdup(line);
    }

    if (!stored_command) {
        return NULL;
    }

    task_t *task = calloc(1, sizeof(*task));
    if (!task) {
        free(stored_command);
        return NULL;
    }

    pthread_mutex_lock(&scheduler_lock);
    task->id = next_task_id++;
    task->arrival_order = next_arrival_order++;
    pthread_mutex_unlock(&scheduler_lock);

    task->client = ctx;
    task->command = stored_command;
    task->type = type;
    task->burst_time = (type == TASK_TYPE_PROGRAM) ? (burst_hint > 0 ? burst_hint : DEFAULT_PROGRAM_BURST) : -1.0;
    task->remaining_time = (type == TASK_TYPE_PROGRAM) ? task->burst_time : -1.0;
    task->output_fd = -1;
    task->streaming = (type == TASK_TYPE_PROGRAM);
    task->sent_len = 0;
    task->next = NULL;
    return task;
}

static void push_ready_task_locked(task_t *task) {
    if (!task) return;
    task->next = NULL;
    if (!ready_head) {
        ready_head = ready_tail = task;
    } else {
        ready_tail->next = task;
        ready_tail = task;
    }
}

static task_t *detach_task_locked(task_t *prev, task_t *task) {
    if (!task) return NULL;
    if (prev) {
        prev->next = task->next;
    } else {
        ready_head = task->next;
    }
    if (ready_tail == task) {
        ready_tail = prev;
    }
    task->next = NULL;
    return task;
}

static task_t *select_next_task_locked(void) {
    if (!ready_head) {
        return NULL;
    }

    task_t *prev = NULL;
    task_t *iter = ready_head;
    while (iter) {
        if (iter->type == TASK_TYPE_SHELL) {
            return detach_task_locked(prev, iter);
        }
        prev = iter;
        iter = iter->next;
    }

    task_t *best = NULL;
    task_t *best_prev = NULL;
    task_t *fallback = NULL;
    task_t *fallback_prev = NULL;
    prev = NULL;
    iter = ready_head;
    while (iter) {
        if (iter->type == TASK_TYPE_PROGRAM) {
            if (!best || iter->remaining_time < best->remaining_time ||
                (iter->remaining_time == best->remaining_time && iter->arrival_order < best->arrival_order)) {
                if (best && best->id != last_scheduled_task) {
                    fallback = best;
                    fallback_prev = best_prev;
                }
                best = iter;
                best_prev = prev;
            } else if (!fallback || iter->remaining_time < fallback->remaining_time ||
                       (iter->remaining_time == fallback->remaining_time && iter->arrival_order < fallback->arrival_order)) {
                fallback = iter;
                fallback_prev = prev;
            }
        }
        prev = iter;
        iter = iter->next;
    }

    if (!best) {
        return NULL;
    }

    if (best->id == last_scheduled_task && fallback) {
        return detach_task_locked(fallback_prev, fallback);
    }

    return detach_task_locked(best_prev, best);
}

static void request_preemption_if_needed(task_t *task) {
    if (!task) {
        return;
    }
    /* Shell tasks always preempt running program tasks */
    if (task->type == TASK_TYPE_SHELL) {
        if (current_task && current_task->type == TASK_TYPE_PROGRAM &&
            current_task->is_running && !current_task->preempt_requested) {
            current_task->preempt_requested = 1;
        }
        return;
    }
    /* Program tasks preempt if they have shorter remaining time */
    if (current_task && current_task->type == TASK_TYPE_PROGRAM && current_task != task &&
        current_task->is_running && !current_task->preempt_requested &&
        current_task->remaining_time > task->remaining_time) {
        current_task->preempt_requested = 1;
    }
}

static void deliver_task_output(task_t *task, int status_code) {
    (void)status_code;
    if (!task || !task->client || task->client->disconnected) {
        return;
    }

    const char *payload = task->output_buffer ? task->output_buffer : "";
    size_t payload_len = task->output_buffer ? task->output_len : 0;

    if (!task->streaming && payload_len > 0) {
        if (send_locked_message(task->client, payload, payload_len) == -1) {
            return;
        }
    }
    if (payload_len > 0) {
        printf(COLOR_CYAN "[%d]<<< %zu bytes sent" COLOR_RESET "\n", task->client->client_id, payload_len);
    }
    send_completion_signal(task);
    printf("(%d)--- " COLOR_RED "ended" COLOR_RESET " (%.0f)\n", task->client->client_id, task->remaining_time);
}

static int start_program_task(task_t *task) {
    if (!task || task->has_started) {
        return 0;
    }

    int fd = -1;
    pid_t pid = spawn_backend_process(task->command, 1, &fd);
    if (pid == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        close(fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    task->pid = pid;
    task->pgid = pid;
    task->output_fd = fd;
    task->has_started = 1;
    task->is_running = 0;
    task->preempt_requested = 0;
    printf("(%d)--- " COLOR_GREEN "started" COLOR_RESET " (%.0f)\n", task->client->client_id, task->burst_time);
    return 0;
}

static void terminate_program_task(task_t *task) {
    if (!task || task->pid <= 0) {
        return;
    }
    killpg(task->pgid > 0 ? task->pgid : task->pid, SIGKILL);
    int st;
    while (waitpid(task->pid, &st, 0) == -1 && errno == EINTR) {
        continue;
    }
    task->pid = -1;
}

static int wait_for_program_status(task_t *task, int *status_out, int options) {
    if (!task || task->pid <= 0) {
        return -1;
    }
    int local_status = 0;
    pid_t result;
    do {
        result = waitpid(task->pid, &local_status, options);
    } while (result == -1 && errno == EINTR);
    if (result == task->pid && status_out) {
        *status_out = local_status;
    }
    return result;
}

static int program_task_finished(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 1;
    }
    return 1;
}

static void finalize_program_task(task_t *task, int status) {
    if (!task) return;
    int exit_status = status;
    drain_task_pipe(task);
    task->completed = 1;
    deliver_task_output(task, exit_status);
    free_task(task);
}

static void process_shell_task(task_t *task) {
    if (!task) return;
    printf("(%d)--- " COLOR_GREEN "started" COLOR_RESET " (-1)\n", task->client->client_id);
    char *output = NULL;
    int status = 1;
    if (run_shell_command(task->command, &output, &status) == -1) {
        output = strdup("Internal server error.\n");
        status = 1;
    }
    if (output) {
        append_output(task, output, strlen(output));
    }
    deliver_task_output(task, status);
    free(output);
    task->completed = 1;
    free_task(task);
}

static int check_cancellation(task_t *task) {
    return !task || task->cancelled || !task->client || task->client->disconnected;
}

static void requeue_program_task(task_t *task) {
    pthread_mutex_lock(&scheduler_lock);
    push_ready_task_locked(task);
    pthread_cond_signal(&scheduler_cond);
    pthread_mutex_unlock(&scheduler_lock);
}


static int finalize_if_completed(task_t *task) {
    if (!task) return 1;
    int status = 0;
    pid_t result = wait_for_program_status(task, &status, WNOHANG);
    if (result == task->pid) {
        task->is_running = 0;
        int exit_status = program_task_finished(status);
        finalize_program_task(task, exit_status);
        return 1;
    }
    if (result == -1 && errno == ECHILD) {
        task->is_running = 0;
        finalize_program_task(task, 0);
        return 1;
    }
    return 0;
}

static void run_program_task_slice(task_t *task) {
    if (!task) return;

    if (start_program_task(task) == -1) {
        append_output(task, "Internal server error.\n", strlen("Internal server error.\n"));
        deliver_task_output(task, 1);
        free_task(task);
        return;
    }

    if (check_cancellation(task)) {
        terminate_program_task(task);
        free_task(task);
        return;
    }

    int quantum = task->first_round_completed ? (int)LATER_ROUND_QUANTUM : (int)FIRST_ROUND_QUANTUM;
    if (quantum <= 0) {
        quantum = 1;
    }

    int elapsed_this_run = 0;
    if (!task->is_running) {
        killpg(task->pgid > 0 ? task->pgid : task->pid, SIGCONT);
        task->is_running = 1;
        printf("(%d)--- " COLOR_CYAN "running" COLOR_RESET " (%.0f)\n", task->client->client_id, task->remaining_time);
    }

    /* Save task_id before any operation that might free the task */
    int saved_task_id = task->id;

    for (int second = 0; second < quantum; second++) {
        if (check_cancellation(task)) {
            gantt_record_stop(saved_task_id, elapsed_this_run);
            terminate_program_task(task);
            free_task(task);
            return;
        }

        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        elapsed_this_run++;
        if (task->remaining_time > 0) {
            task->remaining_time -= 1.0;
            if (task->remaining_time < 0) {
                task->remaining_time = 0;
            }
        }
        drain_task_pipe(task);
        if (task->cancelled) {
            gantt_record_stop(saved_task_id, elapsed_this_run);
            terminate_program_task(task);
            free_task(task);
            return;
        }

        if (finalize_if_completed(task)) {
            gantt_record_stop(saved_task_id, elapsed_this_run);
            pthread_mutex_lock(&scheduler_lock);
            int queue_empty = (ready_head == NULL);
            pthread_mutex_unlock(&scheduler_lock);
            if (queue_empty) {
                gantt_print_and_reset();
            }
            return;
        }

        pthread_mutex_lock(&scheduler_lock);
        int preempt = task->preempt_requested;
        task->preempt_requested = 0;
        pthread_mutex_unlock(&scheduler_lock);
        if (preempt) {
            break;
        }
    }

    if (finalize_if_completed(task)) {
        gantt_record_stop(saved_task_id, elapsed_this_run);
        pthread_mutex_lock(&scheduler_lock);
        int queue_empty = (ready_head == NULL);
        pthread_mutex_unlock(&scheduler_lock);
        if (queue_empty) {
            gantt_print_and_reset();
        }
        return;
    }

    /* Quantum expired or preemption requested; stop task and requeue. */
    if (killpg(task->pgid > 0 ? task->pgid : task->pid, SIGSTOP) == -1) {
        if (errno == ESRCH) {
            gantt_record_stop(saved_task_id, elapsed_this_run);
            finalize_if_completed(task);
            return;
        }
    } else {
        wait_for_program_status(task, NULL, WUNTRACED);
    }
    task->is_running = 0;
    task->first_round_completed = 1;
    drain_task_pipe(task);
    gantt_record_stop(saved_task_id, elapsed_this_run);
    printf("(%d)--- " COLOR_YELLOW "waiting" COLOR_RESET " (%.0f)\n", task->client->client_id, task->remaining_time);
    if (!task->cancelled) {
        requeue_program_task(task);
    } else {
        free_task(task);
    }
}

static void cancel_client_tasks(client_context_t *ctx) {
    if (!ctx) return;
    pthread_mutex_lock(&scheduler_lock);
    task_t *detached = NULL;
    task_t **link = &ready_head;
    ready_tail = NULL;
    while (*link) {
        task_t *task = *link;
        if (task->client == ctx) {
            *link = task->next;
            task->next = detached;
            detached = task;
        } else {
            if (!task->next) {
                ready_tail = task;
            }
            link = &task->next;
        }
    }
    if (!ready_head) {
        ready_tail = NULL;
    }
    if (current_task && current_task->client == ctx) {
        current_task->cancelled = 1;
    }
    pthread_mutex_unlock(&scheduler_lock);

    while (detached) {
        task_t *next = detached->next;
        detached->client = NULL;
        free_task(detached);
        detached = next;
    }
}

static void *scheduler_main(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&scheduler_lock);
        while (!scheduler_shutdown && ready_head == NULL) {
            pthread_cond_wait(&scheduler_cond, &scheduler_lock);
        }
        if (scheduler_shutdown && ready_head == NULL) {
            pthread_mutex_unlock(&scheduler_lock);
            break;
        }
        task_t *task = select_next_task_locked();
        current_task = task;
        pthread_mutex_unlock(&scheduler_lock);

        if (!task) {
            continue;
        }

        int ran_task_id = task->id;
        if (task->type == TASK_TYPE_SHELL) {
            process_shell_task(task);
        } else {
            run_program_task_slice(task);
        }

        pthread_mutex_lock(&scheduler_lock);
        last_scheduled_task = ran_task_id;
        current_task = NULL;
        pthread_mutex_unlock(&scheduler_lock);
    }
    return NULL;
}

/* Per-client service loop: enqueue commands for the scheduler. */
static void handle_client(client_context_t *ctx) {
    while (1) {
        char *command = NULL;
        size_t command_length = 0;

        if (recv_message(ctx->client_fd, &command, &command_length) == -1) {
            free(command);
            break;
        }

        char *trimmed = trim_whitespace(command);
        if (trimmed[0] == '\0') {
            if (send_locked_message(ctx, "", 0) == -1) {
                free(command);
                break;
            }
            free(command);
            continue;
        }

        printf("[%d]>>> %s\n", ctx->client_id, trimmed);

        if (strcmp(trimmed, "exit") == 0) {
            free(command);
            break;
        }

        task_t *task = create_task_from_command(ctx, trimmed);
        if (!task) {
            const char *error_message = "Internal server error.\n";
            send_locked_message(ctx, error_message, strlen(error_message));
            free(command);
            continue;
        }

        printf("(%d)--- " COLOR_BLUE "created" COLOR_RESET " (%.0f)\n", ctx->client_id, task->burst_time);

        pthread_mutex_lock(&scheduler_lock);
        push_ready_task_locked(task);
        request_preemption_if_needed(task);
        pthread_cond_signal(&scheduler_cond);
        pthread_mutex_unlock(&scheduler_lock);

        free(command);
    }
}

/* Thread trampoline that owns a client until disconnect, then frees resources. */
static void *client_thread(void *arg) {
    client_context_t *ctx = arg;
    handle_client(ctx);
    ctx->disconnected = 1;
    cancel_client_tasks(ctx);
    close(ctx->client_fd);
    pthread_mutex_destroy(&ctx->send_lock);
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

    if (pthread_create(&scheduler_thread, NULL, scheduler_main, NULL) != 0) {
        perror("pthread_create (scheduler)");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("-----------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("-----------------------------\n");
    
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
        
        /* Set send timeout to prevent indefinite blocking on abrupt disconnect */
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        ctx->client_fd = client_fd;
        ctx->client_port = ntohs(client_addr.sin_port);
        if (!inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip))) {
            strncpy(ctx->client_ip, "unknown", sizeof(ctx->client_ip) - 1);
            ctx->client_ip[sizeof(ctx->client_ip) - 1] = '\0';
        }
        ctx->disconnected = 0;
        pthread_mutex_init(&ctx->send_lock, NULL);
        
        pthread_mutex_lock(&client_id_lock);
        ctx->client_id = next_client_id++;
        pthread_mutex_unlock(&client_id_lock);
        
        pthread_mutex_lock(&thread_label_lock);
        ctx->thread_label = next_thread_label++;
        pthread_mutex_unlock(&thread_label_lock);
        
        printf(COLOR_CYAN "[%d]<<< client connected" COLOR_RESET "\n", ctx->client_id);
        
        pthread_t thread_handle;
        if (pthread_create(&thread_handle, NULL, client_thread, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            pthread_mutex_destroy(&ctx->send_lock);
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
