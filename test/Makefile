# Makefile

CC = gcc
CFLAGS = -g -lpthread -pthread -D_DEBUG_ -D_TEST_

CLIENT = client.test

SRC_DIR = ../

CLIENT_SRCS = $(SRC_DIR)client.c $(SRC_DIR)module/termui.c \
           $(SRC_DIR)client_service.c \
           $(SRC_DIR)module/sockutil.c $(SRC_DIR)module/fileutil.c $(SRC_DIR)module/timeutil.c \
           $(SRC_DIR)module/queue.c $(SRC_DIR)module/hashmap.c $(SRC_DIR)module/list.c

# 오브젝트 파일
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# 기본 타겟
all: $(CLIENT)

# 타겟 빌드 규칙
$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 소스 파일로부터 오브젝트 파일을 만드는 규칙
%.o: $(SRC_DIR)%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLIENT_OBJS) \
		$(CLIENT)

.PHONY: all clean

