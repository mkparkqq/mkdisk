/*
 * 2024-07-18 Minkeun Park.
 * Socket utility module for t-storage program.
 */

#include "sockutil.h"

#define NONBLCOK_RETRY_LIMIT		10

#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

const char *
sockutil_errstr(enum ERR_SOCKUTIL err) 
{
	if (ERR_SOCKUTIL_RETRY_LIMIT == err)
		return "[send] Retry limit exceeded.";
	else if(ERR_SOCKUTIL_PARTIAL_DATA == err)
		return "[send] Data partially transmitted.";
	else if(ERR_SOCKUTIL_SOCKET == err)
		return "[send] [socket]";
	else if(ERR_SOCKUTIL_SETSOCKOPT == err)
		return "[send] [setsockopt]";
	else if(ERR_SOCKUTIL_SEND_FAILED == err)
		return "[send] [send]";
	else if(ERR_SOCKUTIL_RECV_FAILED == err)
		return "[send] [recv]";
	else if(ERR_SOCKUTIL_SERVER_CLOSED == err)
		return "[send] connection closed";
	else
		return "undefined error occured.";
}

int 
create_tcpsock()
{
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return ERR_SOCKUTIL_SOCKET;
	int reuse = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
		return ERR_SOCKUTIL_SETSOCKOPT;

	return sockfd;
}

/*
 * Send routine for a tcp socket(.sockfd). Blocking call.
 * Need to convert the data(.data) to network byte order.
 *
 * @param sockfd - Receiver.
 * @param data - Data buffer address to send.
 * @param dlen - Data length to send.
 * @return - Error : ERR_SOCKUTIL_PARTIAL_DATA, ERR_SOCKUTIL_SEND_FAILED.
 		   - Success : The number of times the send	function have been called.
 */
int64_t 
send_stream(int sockfd, void *data, int64_t dlen)
{
	int64_t slen = 0;
	int sendcnt = 0;
	while (slen < dlen) {
		++sendcnt;
		ssize_t chunk = send(sockfd, data + slen, dlen - slen, 0);
		if (chunk < 0)
			return ERR_SOCKUTIL_SEND_FAILED;
		slen += (int64_t)chunk;
	}

	if (slen != dlen) {
		return ERR_SOCKUTIL_PARTIAL_DATA;
	}

	return sendcnt;
}

static void
update_trans_stat(struct trans_stat *stat, int64_t sent)
{
	if (NULL != stat)
		stat->transmitted = sent;
}

/*
 * Send routine for a tcp socket(.sockfd). Non-blocking call.
 * Need to convert the data(.data) to network byte order.
 *
 * @param sockfd - Receiver.
 * @param data - Data buffer address to send.
 * @param dlen - Data length to send.
 * @param rate - Transmission status in real-time.
 * @return - Error : SEND_RETRY_EXCEEDED, ERR_SOCKUTIL_PARTIAL_DATA.
 		   - Success : The number of times the send	function have been called.
 */
int64_t 
send_stream_nblock(int sockfd, void *data, int64_t dlen, struct trans_stat *rate)
{
	if (NULL != rate) {
		rate->total = dlen;
		rate->transmitted = 0;
	}

	struct timeval timeout;
	int64_t slen = 0;
	int sendcnt = 0;
	int retrycnt = 0;
	fd_set writefds;

	while (slen < dlen) {
		FD_ZERO(&writefds);
		FD_SET(sockfd, &writefds);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		int result = select(sockfd + 1, NULL, &writefds, NULL, &timeout);

		if (result > 0) {
			if (FD_ISSET(sockfd, &writefds)) {
				++sendcnt;
				int64_t chunk = send(sockfd, data + slen, dlen - slen, MSG_DONTWAIT);
				if (chunk < 0) {
					update_trans_stat(rate, -1);
					return ERR_SOCKUTIL_SEND_FAILED;
				}
				// Successfully sent.
				slen += chunk;
				update_trans_stat(rate, slen);
				retrycnt = 0;
			}
		} else if (0 == result){
			if (NONBLCOK_RETRY_LIMIT == ++retrycnt) {
				update_trans_stat(rate, -1);
				return ERR_SOCKUTIL_RETRY_LIMIT;
			}
			continue;
		}
	}

	if (slen != dlen)
		return ERR_SOCKUTIL_PARTIAL_DATA;

	return sendcnt;
}

void 
set_sockaddr_in(const char *ip, int port, struct sockaddr_in *sa)
{
	sa->sin_family = AF_INET;
	if (NULL == ip)
		sa->sin_addr.s_addr = INADDR_ANY;
	else
		inet_pton(AF_INET, ip, &sa->sin_addr);
	sa->sin_port = htons(port);
}

void 
set_sock_nonblock(int sockfd)
{
	int flags = fcntl(sockfd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(sockfd, F_SETFL, flags);
}

void 
set_sock_block(int sockfd)
{
	int flags = fcntl(sockfd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(sockfd, F_SETFL, flags);
}

int
set_socket_timeout(int sockfd, int sec)
{
	struct timeval timeout;
	timeout.tv_sec = sec;
	timeout.tv_usec = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		return -1;
	return 0;
}

int
recv_stream_nblock(int sockfd, void *buf, int64_t dlen, struct trans_stat *rate)
{
	int ret = 0;
	ssize_t rlen = 0;
	int cnt = 0;
	if (NULL != rate)
		rate->total = dlen;
	
	while (rlen < dlen) {
		cnt++;
		ssize_t chunk = recv(sockfd, buf + rlen, dlen - rlen, 0);
		if (EWOULDBLOCK == errno || EAGAIN == errno) {
			if (++cnt == NONBLCOK_RETRY_LIMIT) {
				update_trans_stat(rate, -1);
				return ERR_SOCKUTIL_RETRY_LIMIT;
			}
			continue;
		} else if (0 == chunk){
			update_trans_stat(rate, -1);
			return ERR_SOCKUTIL_SERVER_CLOSED;
		} else if (chunk < 0) {
			update_trans_stat(rate, -1);
			return ERR_SOCKUTIL_RECV_FAILED;
		}
		cnt = 0;
		rlen += (ssize_t)chunk;
		update_trans_stat(rate, rlen);
	}

	if (rlen == dlen)
		return 0;
	else {
		update_trans_stat(rate, -1);
		return ERR_SOCKUTIL_PARTIAL_DATA;
	}
}

