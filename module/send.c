#include "sockutil.h"
#include "fileutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define FILE_SIZE_MAX			(5LL * 1024LL * 1024LL * 1024LL) // 5GiB
#define EXIT_INVAL_USAGE		1
#define EXIT_SYSCALL_FAILED		2
#define EXIT_FATAL				3
#define STDIN_FILENO			0

static int
win_column(int fd)
{
	struct winsize size;
	if (ioctl(fd, TIOCGWINSZ, (char*) &size) < 0)
		return -1;
	return size.ws_col;
}

/*
 * ./send [server ip] [port number]
 */
static int
check_usage(int argc, const char *argv[])
{
	char usage[64] = {0, };
	snprintf(usage, 64, "Usage: %s [server ip] [port number]\n", argv[0]);

	if (3 != argc) {
		fputs(usage, stdout);
		return -1;
	}
	int port = strtol(argv[2], NULL, 10);
	if (errno == EINVAL) {
		fputs(usage, stdout);
		return -1;
	}
	return 0;
}

static int64_t
send_file(int sockfd, const char *path, struct trans_stat *rate)
{
	int64_t flen = 0;
	int64_t dlen = 0;		// Size of data to send in bytes.
	int64_t slen = 0;		// Size of data sent in bytes.

	flen = sizeof_file(path);
	if (flen > FILE_SIZE_MAX)
		return -1;

	void *data = malloc(flen);
	if (NULL == data)
		return -1;

	dlen = read_file(path, data, flen);
	if (dlen < 0)
		goto futil_err;

	int64_t netdata = htobe64(dlen);
	slen = send_stream(sockfd, &netdata, sizeof(int64_t));
	if (slen < 0)
		goto sockutil_err;

	slen = send_stream_nblock(sockfd, data, dlen, rate);
	if (slen < 0)
		goto sockutil_err;

	// printf("\n\nsend count : %ld\n\033[A\033[A\033[A", slen);

	free(data);

	return slen;

futil_err:
	printf("[send_file] %s\n", futil_errstr(dlen));
	free(data);
	return -1;

sockutil_err:
	printf("[send_file] %s\n", sockutil_errstr(slen));
	free(data);
	return -1;
}

static void*
print_pbar(void* p) 
{
	struct trans_stat *stat = (struct trans_stat *)p;
	double progress = 0.0;

	printf("\033[?25l");
	while (1) {
		fflush(stdout);
		if (stat->transmitted < 0)
			return NULL;
		if (0 == stat->total)
			continue;
		progress = (double) stat->transmitted / (double) stat->total * 100.0F;
		printf("\033[G[%3.f%%]", progress);
		int barlen = (win_column(STDIN_FILENO) - 5) * progress / 100;
		for (int i = 0; i < barlen; i++)
			printf("#");
		fflush(stdout);
		if (stat->total == stat->transmitted)
			break;
	}
	progress = (double) stat->transmitted / (double) stat->total * 100.0F;
	printf("\n%ld Bytes transmitted\n\033[?25h", stat->transmitted);
}


int
main(int argc, const char *argv[])
{
	if (check_usage(argc, argv) < 0)
		return EXIT_INVAL_USAGE;

	int sockfd = create_tcpsock();
	if ( sockfd < 0) {
		perror("[socket]");
		return EXIT_SYSCALL_FAILED;
	}

	struct sockaddr_in sa;
	set_sockaddr_in(argv[1], atoi(argv[2]), &sa);
	if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("[connect]");
		return EXIT_SYSCALL_FAILED;
	}

	struct trans_stat rate = { 0, 0 };
	pthread_t pbar_worker = 0;
	pthread_create(&pbar_worker, NULL, print_pbar, (void *)&rate);

	// int64_t scnt = send_file(sockfd, "./sample-image.jpg", &rate); // 13MiB
	int64_t scnt = send_file(sockfd, "./gallery.mydb", &rate);   // 4GiB

	pthread_join(pbar_worker, NULL);

	printf("send iteration: %ld\n", scnt);

	return 0;
}

