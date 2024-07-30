CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS = -pthread

# Paths
APUE_DIR = /home/mkpark/apue

# Targets
CLIENT_TARGET = send
SERVER_TARGET = server

# Source files
CLIENT_SRC = send.c
SERVER_SRC = server.c

# Object files
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
UTIL_OBJS = fileutil.o sockutil.o \
			queue.o hashmap.o queue.o

# Rules
all: $(CLIENT_TARGET) $(SERVER_TARGET)

$(CLIENT_TARGET): $(CLIENT_OBJ) $(UTIL_OBJS) 
	$(CC) $(LDFLAGS) -o $@ $^

$(SERVER_TARGET): $(SERVER_OBJ) $(UTIL_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(CLIENT_OBJ): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_OBJ): $(SERVER_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(CLIENT_TARGET) $(SERVER_TARGET) \
		$(CLIENT_OBJ) $(SERVER_OBJ) $(UTIL_OBJS) 

# Phony target
.PHONY: all clean

