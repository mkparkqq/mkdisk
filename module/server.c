#include "sockutil.h"
#include "fileutil.h"
#include "../../apue/apue.h"

#include <netdb.h>
#include <errno.h>
#include <syslog.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>

#define FILE_SIZE_MAX 	(5LL * 1000LL * 1000LL * 1000LL) // 5GB
#define QLEN			10
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX	256
#endif

int 
initserver(int type, const struct sockaddr *addr, socklen_t alen, int qlen)
{
	int fd;
	int err = 0;

	if ((fd = socket(addr->sa_family, type, 0)) < 0)
		return -1;
	if (bind(fd, addr, alen) < 0)
		goto errout;
	if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
		if (listen(fd, qlen) < 0)
			goto errout;
	}
	return fd;

errout:
	err = errno;
	close(fd);
	errno = err;
	return -1;
}

int
set_cloexec(int fd)
{
	int val;
	if ((val = fcntl(fd, F_GETFD, 0)) < 0) 
		return -1;
	val |= FD_CLOEXEC;

	return (fcntl(fd, F_SETFD, val));
}


void
serve(int sockfd)
{
	int clfd;

	socklen_t clilen;
	struct sockaddr_in cli_addr;

	if (listen(sockfd, 5) < 0) {
		perror("[listen]");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	printf("Listening... \n");

	for (;;) {
		if ((clfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0) {
			syslog(LOG_ERR, "ruptimed: accept error: %s",
					strerror(errno));
			exit(1);
		}
		printf("\033[32mClient connected from %s:%d\n\033[0m",
				inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
		// Receive file size.
		uint64_t netdata = 0;
		if (recv(clfd, &netdata, sizeof(uint64_t), 0) < 0) {
			perror("[recv] file length");
			return;
		}
		uint64_t flen = be64toh(netdata);
		printf("%luBytes\n", flen);

		// Receive file data.
		void *buf = malloc(flen);
		if (NULL == buf) {
			perror("[serve] [malloc]");
			return;
		}

		ssize_t rlen = 0;
		int cnt = 0;
		printf("\n");
		while (rlen < flen) {
			cnt++;
			// ssize_t chunk = recv(clfd, buf + rlen, flen - rlen, MSG_DONTWAIT);
			ssize_t chunk = recv(clfd, buf + rlen, flen - rlen, 0);
			if (EWOULDBLOCK == errno || EAGAIN == errno) {
				perror("[recv]");
				if (++cnt == 10) {
					printf("[serve] [recv] timeout\n");
					break;
				}
				usleep(1000 * 500);
				continue;
			} else if (0 == chunk){
				printf("client closed\n");
				break;
			} else if (chunk < 0) {
				fprintf(stderr, "[serve] [recv] %s\n", strerror(errno));
				break;
			}
			printf("\033[A\033[G[%3.f%%]\n", (double) rlen / (double) flen * 100);
			rlen += (ssize_t)chunk;
		}
		if (rlen == flen)
			printf("Download complete(%d). (%ldB/%ldB)\n", cnt, rlen, flen);
		else
			printf("Download incomplete(%d). (%ldB/%ldB)\n", cnt, rlen, flen);
		free(buf);
		close(clfd);
	}
}

int
main(int argc, const char *argv[])
{
	int sockfd;
	struct sockaddr_in serv_addr;
	int portno = 23455;

	// Open socket file.
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("[socket]");
		exit(EXIT_FAILURE);
	}
	int option = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(int));

	// Initialize socket address structure.
	memset((char *)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);

	// Binding socket address to socket file.
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("[bind]");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
	printf("[address] %s:%d\n", 
			inet_ntoa(serv_addr.sin_addr), portno);

	serve(sockfd);

	return 0;
}

