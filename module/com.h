/*
 * 2024-07-17 Minkeun Park.
 * Communication module for t-storage program.
 */
#ifndef _COM_H_
#define _COM_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

struct trans_stat {
	int64_t total;
	int64_t transmitted;
};

enum ERR_SEND {
	SEND_RETRY_EXCEEDED = -1,
	SEND_PARTIAL_DATA = -2
};

int64_t send_file_data(int sockfd, const char *path, struct trans_stat *rate);
void setsockaddr(const char *ip, int port, struct sockaddr_in *sa);
int ifaddrstr(const char *interface, char *ip, size_t iplen);

#endif
