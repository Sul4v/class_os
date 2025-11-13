# Project Phase 2 Report – MyShell Remote

## Title Page
- Phase: Project Phase 2
- Project: MyShell Remote (client/server)
- Authors: Bimarsha Adhikari, Sulav G Shrestha

## Architecture and Design
- Components
  - Local shell (Phase 1): `myshell.c`, `parser.c/.h`, `executor.c/.h`, `error_handler.c/.h`
  - Networking layer (Phase 2): `myshell_server.c`, `myshell_client.c`, `network_utils.c/.h`
  - Build system: `Makefile`
- How it works (plain language)
  1) The client connects to the server over TCP (default 127.0.0.1:5050)
  2) The client shows a `$` prompt, reads a command line, and sends it
  3) The server receives the line, runs it using the same shell code from Phase 1
  4) The server sends back the command’s output as a single text response
  5) The client prints the response and asks for the next command until `exit`
- Protocol choice
  - We send a 32‑bit length first, then the bytes of the message. This makes reading reliable and simple.

## Implementation Highlights
- Shell features
  - Commands with/without arguments, quoting and escaping
  - Redirections: `<`, `>`, `2>`
  - Pipes with multiple stages
  - Unquoted wildcard expansion (e.g., `*.txt`)
- Execution model
  - Each command runs in a child process using `fork`/`execvp`
  - Pipes and redirections are set up with `pipe`/`dup2`
- Networking pieces
  - `network_utils.c/.h`: small helpers for sending/receiving framed messages
  - `myshell_server.c`: accepts multiple clients concurrently using a thread-per-connection model, logs every request/response with `[Client #n - ip:port]` context, runs commands, sends results
  - `myshell_client.c`: connects, shows prompt, sends lines, prints server replies
- Server log format (examples)
  - `[INFO] Client #1 connected from 192.168.1.100:56789. Assigned to Thread-1.`
  - `[RECEIVED] [Client #2 - 192.168.1.101:56790] Received command: "ls -l"`
  - `[EXECUTING] [Client #2 - 192.168.1.101:56790] Executing command: "ls -l"`
  - `[OUTPUT] [Client #2 - 192.168.1.101:56790] Sending output to client:`
  - On unknown commands: `[ERROR] [Client #2 - 192.168.1.101:56790] Command not found: "unknowncmd"`

## Execution Instructions
```bash
make clean && make
# Terminal A (server)
./server        # uses $MYSHELL_PORT or 5050
# Terminal B (client)
./client        # uses $MYSHELL_HOST/$MYSHELL_PORT or 127.0.0.1:5050
```

Set `MYSHELL_PORT` (and optionally `MYSHELL_HOST` for the client) to override the defaults without passing command-line arguments.

## Phase 3 Enhancements
- Thread-per-client server so multiple users can issue commands simultaneously
- Mandatory, contextual logging for every inbound/outbound message, matching the rubric format
- Robust error reporting per client (internal errors, command-not-found, disconnects)

## Testing
- Basic
  - `echo hello` → client prints `hello`
  - `ls -la` → client shows directory listing
- Redirections and globbing
  - `echo "join"ed > a2` → file `a2` contains `joined`
  - `echo '*.txt'` (literal) vs `echo *.txt` (expanded)
- Pipelines
  - `cat < input | grep "Hello" | sort | uniq > output` → deduplicated sorted matches in `output`
- Invalid commands
  - `unknowncmd` → client shows `Command not found: unknowncmd`; server logs the same
  - `echo hi | invalid_cmd` → same behavior for the invalid stage

## Division of Tasks
- Networking and server/client integration: Bimarsha Adhikari
- Shell parsing/execution and validation: Sulav G Shrestha
- End‑to‑end testing, Makefile, and report: Both

## References
- Linux man pages: `fork`, `execvp`, `dup2`, `pipe`, `waitpid`, `socket`, `bind`, `listen`, `accept`, `connect`
- Course specifications for Phase 1 and Phase 2
