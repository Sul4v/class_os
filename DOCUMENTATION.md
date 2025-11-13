# MyShell - Complete Shell Implementation

## Overview

MyShell is a complete shell implementation in C that supports all the features required by the project specification. It provides a Unix-like command-line interface with support for command execution, input/output redirection, pipes, and comprehensive error handling.

## Architecture

The codebase is designed with modularity and maintainability in mind, following best practices in C programming:

### File Structure

```
myshell/
├── myshell.h           # Main header with all structure definitions and includes
├── myshell.c           # Main program loop and shell initialization
├── parser.h/parser.c   # Command line parsing and tokenization
├── executor.h/executor.c # Command execution, pipes, and redirections
├── error_handler.h/error_handler.c # Error handling and validation
├── Makefile           # Build system with multiple targets
└── DOCUMENTATION.md   # This file
```

### Core Components

#### 1. Main Shell Loop (`myshell.c`)
- **Purpose**: Provides the main shell interface and control flow
- **Key Functions**:
  - `shell_loop()`: Main interactive loop that displays prompts and processes commands
  - `main()`: Entry point with signal handling setup
- **Features**:
  - Displays `$` prompt like standard Unix shells
  - Handles EOF (Ctrl+D) gracefully
  - Processes "exit" command to terminate shell
  - Ignores SIGINT (Ctrl+C) for shell process itself

#### 2. Command Parser (`parser.c`)
- **Purpose**: Converts command line strings into structured data for execution
- **Key Structures**:
  - `token_t`: Represents individual tokens (words, operators)
  - `command_t`: Single command with arguments and redirections
  - `pipeline_t`: Series of commands connected by pipes
- **Key Functions**:
  - `tokenize()`: Breaks input into tokens (words, |, <, >, 2>)
  - `parse_command_line()`: Main parsing function
  - `parse_tokens()`: Converts tokens into command structures
- **Features**:
  - Handles all redirection operators (<, >, 2>)
  - Supports unlimited number of pipes
  - Comprehensive error detection during parsing

#### 3. Command Executor (`executor.c`)
- **Purpose**: Executes parsed commands using system calls
- **Key Functions**:
  - `execute_pipeline()`: Coordinates execution of command pipelines
  - `execute_single_command()`: Handles single command execution
  - `setup_redirections()`: Manages file redirections using dup2()
  - `create_pipes()`: Creates pipe file descriptors
- **Features**:
  - Uses fork()/exec()/wait() for process management
  - Implements pipes using pipe() system call
  - Handles file redirections with proper error checking
  - Supports executable programs (like ./hello)

#### 4. Error Handler (`error_handler.c`)
- **Purpose**: Provides consistent error handling and validation
- **Key Functions**:
  - `validate_pipeline()`: Checks pipeline for logical errors
  - `print_error()`: Consistent error message formatting
  - `print_parse_error()`: Parse-specific error messages
- **Features**:
  - Validates file accessibility before execution
  - Checks for empty commands and missing arguments
  - Provides helpful error messages matching the specification

## Supported Features

### 1. Basic Commands
- **Single commands**: `ls`, `ps`, `cat`, etc.
- **Commands with arguments**: `ls -la`, `ps aux`, `cat file.txt`
- **Custom executables**: `./hello`, `./myprogram`

### 2. Input/Output Redirection
- **Input redirection**: `command < input.txt`
- **Output redirection**: `command > output.txt`
- **Error redirection**: `command 2> error.log`

### 3. Pipes
- **Single pipe**: `command1 | command2`
- **Multiple pipes**: `command1 | command2 | command3 | ...`
- **Unlimited pipe support**: No hardcoded limits on pipe count

### 4. Compound Commands
All combinations specified in the requirements:
- `command < input.txt`
- `command > output.txt`
- `command 2> error.log`
- `command1 < input.txt | command2`
- `command1 | command2 > output.txt`
- `command1 | command2 2> error.log`
- `command < input.txt > output.txt`
- `command1 < input.txt | command2 > output.txt`
- `command1 < input.txt | command2 | command3 > output.txt`
- `command1 | command2 | command3 2> error.log`
- `command1 < input.txt | command2 2> error.log | command3 > output.txt`

### 5. Error Handling
Complete error detection and reporting:
- **Missing files**: Input file not found
- **Missing arguments**: Redirection without filename
- **Invalid commands**: Command not found in PATH
- **Parse errors**: Empty commands, malformed pipes
- **Permission errors**: File access problems

## Implementation Details

### Memory Management
- Dynamic allocation for command arguments and structures
- Proper cleanup with `free_pipeline()` after each command
- No memory leaks in normal operation

### Process Management
- Parent process waits for all children to complete
- Proper signal handling for child processes
- Clean exit codes and status reporting

### File Descriptor Management
- Careful opening/closing of file descriptors
- Proper dup2() usage for redirections
- Clean pipe file descriptor handling

### Error Resilience
- Graceful handling of system call failures
- Meaningful error messages for users
- Continued operation after errors (doesn't crash)

## Building and Usage

### Compilation
```bash
make clean && make
```

### Running
```bash
./myshell
```

### Remote Mode (optional Phase 2 feature)
```bash
make
# Terminal A
./server        # listens on $MYSHELL_PORT or 5050
# Terminal B
./client        # connects to $MYSHELL_HOST:$MYSHELL_PORT (defaults to 127.0.0.1:5050)
```

You can override the defaults without extra arguments by exporting `MYSHELL_HOST` and/or `MYSHELL_PORT`.  
The server spins up one thread per client so multiple remote shells can run concurrently; every inbound
and outbound message is logged in the format `[TAG] [Client #n - ip:port] ...` for grading compliance.

### Example Session
```bash
$ ls -la
[directory listing]
$ cat file.txt | grep pattern > results.txt
$ echo "Hello" | ./myprogram > output.txt
$ exit
```

## Code Quality Features

### 1. Modular Design
- Clear separation of concerns
- Reusable functions across modules
- Well-defined interfaces between components

### 2. Comprehensive Comments
- Detailed function documentation
- Explanation of complex algorithms
- Clear variable and structure naming

### 3. Error Handling
- Systematic error checking
- Consistent error message formatting
- Graceful degradation on failures

### 4. Maintainability
- Clean code structure
- Easy to extend with new features
- Well-organized file hierarchy

## Testing Coverage

The implementation has been thoroughly tested with:
- All basic command types
- All redirection combinations
- All pipe scenarios
- All error conditions from requirements
- Custom executable programs
- Edge cases and malformed input

## Standards Compliance

- **C99 Standard**: Uses modern C features appropriately
- **POSIX Compliance**: Uses standard Unix system calls
- **Linux Compatibility**: Tested on Linux environment
- **Best Practices**: Follows C programming best practices
