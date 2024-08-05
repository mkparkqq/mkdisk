# Makefile

# 컴파일러와 플래그
CC = gcc
CFLAGS = -g -lpthread -pthread -D_DEBUG_

# 타겟 실행 파일
CLIENT = client.out
SERVER = server.out

# 소스 파일
CLIENT_SRCS = client.c module/termui.c \
			  client_service.c \
			  module/sockutil.c module/fileutil.c module/timeutil.c \
			  module/queue.c module/hashmap.c module/list.c

SERVER_SRCS = server.c \
			  server_service.c \
			  module/sockutil.c module/fileutil.c module/timeutil.c \
			  module/queue.c module/hashmap.c module/list.c

# 오브젝트 파일
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# 기본 타겟
all: $(CLIENT) $(SERVER)

# 타겟 빌드 규칙
$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 소스 파일로부터 오브젝트 파일을 만드는 규칙
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLIENT_OBJS) $(SERVER_OBJS)

.PHONY: all clean

