# Makefile for myshell project
# Compiles a modular shell implementation with proper dependency management

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g
TARGET = myshell

# Source files
SOURCES = myshell.c parser.c executor.c error_handler.c
HEADERS = myshell.h parser.h executor.h error_handler.h
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET)

# Build the main executable
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

# Compile individual object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Force rebuild
rebuild: clean all

# Install (optional - copies to /usr/local/bin)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Create a debug version with additional debug symbols
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET)

# Create an optimized release version
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET)

# Check for memory leaks (requires valgrind)
memcheck: $(TARGET)
	valgrind --leak-check=full --track-origins=yes ./$(TARGET)

# Run basic tests
test: $(TARGET)
	@echo "Running basic shell tests..."
	@echo "ls" | ./$(TARGET)
	@echo "echo hello world" | ./$(TARGET)
	@echo "exit" | ./$(TARGET)

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build the shell (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  rebuild  - Clean and build"
	@echo "  debug    - Build with debug symbols"
	@echo "  release  - Build optimized version"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  memcheck - Run with valgrind"
	@echo "  test     - Run basic tests"
	@echo "  help     - Show this help"

# Mark phony targets
.PHONY: all clean rebuild install debug release memcheck test help
