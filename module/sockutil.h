/*
 * 2024-07-18 Minkeun Park.
 * Socket utility module for t-storage program.
 */
#ifndef _SOCKUTIL_H_
#define _SOCKUTIL_H_
#define _DEFAULR_SOURCE

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

enum ERR_SOCKUTIL {
	ERR_SOCKUTIL_RETRY_LIMIT = -1,
	ERR_SOCKUTIL_PARTIAL_DATA = -2,
	ERR_SOCKUTIL_SOCKET = -3,
	ERR_SOCKUTIL_SETSOCKOPT = -4,
	ERR_SOCKUTIL_SEND_FAILED = -5,
	ERR_SOCKUTIL_RECV_FAILED = -6,
	ERR_SOCKUTIL_SERVER_CLOSED = -7
};

struct trans_stat {
	int64_t total;
	int64_t transmitted;
};

// int64_t send_file_data(int sockfd, const char *path, struct trans_stat *rate);

const char * sockutil_errstr(enum ERR_SOCKUTIL err);

int create_tcpsock();

/*
 * Send data(.data) to the endpoint(.sockfd).
 *
 * @param sockfd - Receiver.
 */
int64_t send_stream(int sockfd, void *data, int64_t dlen);

/*
 * Send data(.data) to the endpoint(.sockfd).
 *
 * @param sockfd - Receiver.
 * @param rate - Transmission status in real-time.
 */
int64_t send_stream_nblock(int sockfd, void *data, int64_t dlen, struct trans_stat *rate);

void set_sockaddr_in(const char *ip, int port, struct sockaddr_in *sa);

void set_sock_nonblock(int sockfd);

int set_socket_timeout(int, int);

int recv_stream_nblock(int sockfd, void *buf, int64_t dlen, struct trans_stat *rate);

#endif // _SOCKUTIL_H_
