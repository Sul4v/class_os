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
  - `myshell_server.c`: accepts one client at a time, logs steps, runs commands, sends results
  - `myshell_client.c`: connects, shows prompt, sends lines, prints server replies
- Server log format (examples)
  - `[INFO] Server started, waiting for client connections...`
  - `[RECEIVED] Received command: "ls -l" from client.`
  - `[EXECUTING] Executing command: "ls -l"`
  - `[OUTPUT] Sending output to client:`
  - On unknown commands: `[ERROR] Command not found: "unknowncmd"`

## Execution Instructions
```bash
make clean && make
# Terminal A (server)
./myshell_server 5050
# Terminal B (client)
./myshell_client 127.0.0.1 5050
```

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
