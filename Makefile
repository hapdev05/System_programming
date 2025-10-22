CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g
LDFLAGS = -pthread

# Directories
SRC_DIR = .
SERVER_DIR = server
CLIENT_DIR = client
COMMON_DIR = common

# Source files
SERVER_SOURCES = $(SERVER_DIR)/server.c $(COMMON_DIR)/utils.c
CLIENT_SOURCES = $(CLIENT_DIR)/client.c $(COMMON_DIR)/utils.c

# Object files
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# Executables
SERVER_EXEC = chat_server
CLIENT_EXEC = chat_client

# Default target
all: $(SERVER_EXEC) $(CLIENT_EXEC)

# Server target
$(SERVER_EXEC): $(SERVER_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

# Client target
$(CLIENT_EXEC): $(CLIENT_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(SERVER_OBJECTS) $(CLIENT_OBJECTS) $(SERVER_EXEC) $(CLIENT_EXEC)

# Install (copy executables to /usr/local/bin)
install: $(SERVER_EXEC) $(CLIENT_EXEC)
	sudo cp $(SERVER_EXEC) /usr/local/bin/
	sudo cp $(CLIENT_EXEC) /usr/local/bin/

# Install to user local bin (no sudo required)
install-user: $(SERVER_EXEC) $(CLIENT_EXEC)
	@mkdir -p $(HOME)/.local/bin
	cp $(SERVER_EXEC) $(HOME)/.local/bin/
	cp $(CLIENT_EXEC) $(HOME)/.local/bin/
	@echo "Installed to $(HOME)/.local/bin"
	@echo "Make sure $(HOME)/.local/bin is in your PATH"

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/$(SERVER_EXEC) /usr/local/bin/$(CLIENT_EXEC)

# Run server
run-server: $(SERVER_EXEC)
	./$(SERVER_EXEC)

# Run client (with default server)
run-client: $(CLIENT_EXEC)
	./$(CLIENT_EXEC)

# Run client with custom server
run-client-custom: $(CLIENT_EXEC)
	@echo "Usage: make run-client-custom SERVER_IP=<ip> SERVER_PORT=<port>"
	@echo "Example: make run-client-custom SERVER_IP=192.168.1.100 SERVER_PORT=8080"
	./$(CLIENT_EXEC) $(SERVER_IP) $(SERVER_PORT)

# Test the system
test: $(SERVER_EXEC) $(CLIENT_EXEC)
	@echo "Starting server in background..."
	./$(SERVER_EXEC) &
	SERVER_PID=$$!
	sleep 2
	@echo "Starting test client..."
	./$(CLIENT_EXEC) &
	CLIENT_PID=$$!
	sleep 5
	@echo "Stopping test..."
	kill $$SERVER_PID $$CLIENT_PID 2>/dev/null || true

# Debug build
debug: CFLAGS += -DDEBUG -g3
debug: $(SERVER_EXEC) $(CLIENT_EXEC)

# Release build
release: CFLAGS += -O2 -DNDEBUG
release: clean $(SERVER_EXEC) $(CLIENT_EXEC)

# Help
help:
	@echo "Available targets:"
	@echo "  all              - Build both server and client"
	@echo "  $(SERVER_EXEC)   - Build server only"
	@echo "  $(CLIENT_EXEC)   - Build client only"
	@echo "  clean            - Remove build files"
	@echo "  install          - Install executables to system (requires sudo)"
	@echo "  install-user     - Install to ~/.local/bin (no sudo needed)"
	@echo "  uninstall        - Remove executables from system"
	@echo "  run-server       - Run server"
	@echo "  run-client       - Run client (localhost:8080)"
	@echo "  run-client-custom- Run client with custom server"
	@echo "  test             - Run automated test"
	@echo "  debug            - Build with debug symbols"
	@echo "  release          - Build optimized release"
	@echo "  help             - Show this help"

.PHONY: all clean install install-user uninstall run-server run-client run-client-custom test debug release help
