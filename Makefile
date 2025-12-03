# Makefile for myshell project
# Compiles a modular shell implementation with proper dependency management

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g

TARGET = myshell
SERVER = server
CLIENT = client
DEMO = demo

CORE_SOURCES = parser.c executor.c error_handler.c
CORE_OBJECTS = $(CORE_SOURCES:.c=.o)

CLIENT_SOURCES = myshell_client.c network_utils.c
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

SERVER_SOURCES = myshell_server.c network_utils.c
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
SERVER_LIBS = -lpthread

HEADERS = myshell.h parser.h executor.h error_handler.h network_utils.h

all: $(TARGET) $(SERVER) $(CLIENT) $(DEMO)

$(TARGET): myshell.o $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $^

$(SERVER): $(SERVER_OBJECTS) $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $(SERVER) $(filter %.o,$^) $(SERVER_LIBS)

$(CLIENT): $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJECTS)

$(DEMO): demo.o
	$(CC) $(CFLAGS) -o $(DEMO) demo.o

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET) $(SERVER) $(CLIENT) $(DEMO) myshell_server myshell_client

# Force rebuild
rebuild: clean all

# Install (optional - copies to /usr/local/bin)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Create a debug version with additional debug symbols
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET) $(SERVER) $(CLIENT)

# Create an optimized release version
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET) $(SERVER) $(CLIENT)

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
