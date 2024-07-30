#include "service.h"
#include "timeutil.h"
#include "hashmap.h"
#include "queue.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERVER_RESP_TIMEOUT		5

#ifdef _CLIENT_H_
char svc_errinfo[ERRSTR_LEN];

static int
send_svc_req(int sockfd, const char *path, int64_t flen,  enum ACCESS_LEVEL alv)
{
	int64_t slen = 0;
	char *fname = strrchr(path, '/');
	if (NULL == fname)
		fname = path;
	else
		fname += sizeof(char);

	struct svc_req req;
	snprintf(req.type, SVC_TYPE_LEN, "%d", SVC_UPLOAD);
	strncpy(req.fname, fname, FILE_NAME_LEN);
	snprintf(req.flen, REQ_FLEN_LEN, "%d", flen);
	snprintf(req.alv, REQ_ALV_LEN, "%d", alv);

	slen = send_stream(sockfd, &req, sizeof(struct svc_req));
	 if (slen < 0) {
		 strncpy(svc_errinfo, sockutil_errstr(slen), ERRSTR_LEN);
		 return -1;
	 }
	 return 0;
}

static int
send_file(int sockfd, const char *path, int64_t flen, struct trans_stat *rate)
{
	int64_t dlen = 0;// Size of data to send in bytes.
	int64_t slen = 0;// Size of data sent in bytes.

	void *data = malloc(flen);
	if (NULL == data) {
		 strncpy(svc_errinfo, "[malloc]", ERRSTR_LEN);
		 return -1;
	}

	dlen = read_file(path, data, flen);
	if (dlen < 0) {
		strncpy(svc_errinfo, "[read_file]", ERRSTR_LEN);
		goto futil_err;
	}

	slen = send_stream_nblock(sockfd, data, dlen, rate);
	if (slen < 0) {
		strncpy(svc_errinfo, sockutil_errstr(slen), ERRSTR_LEN);
		goto sockutil_err;
	}

	free(data);

	return 0;

futil_err:
	free(data);
	return -1;

sockutil_err:
	free(data);
	return -1;
}

char *
svc_errstr() 
{ 
	return svc_errinfo; 
}

int 
client_upload_service(int sockfd, const char *path, 
		int64_t flen, enum ACCESS_LEVEL alv, 
		struct trans_stat *rate)
{
	// Send svc_req.
	if (send_svc_req(sockfd, path, flen, alv) < 0) {
		strncpy(svc_errinfo, "[send_svc_req]", ERRSTR_LEN);
		return -1;
	}

	if (set_socket_timeout(sockfd, SERVER_RESP_TIMEOUT) < 0) {
		strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
		return -1;
	}

	printf("\033[2KWait...");
	fflush(stdout);
	// Receive svc_resp.
	struct svc_resp resp;
	if (recv(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno) 
			strncpy(svc_errinfo, "Server is busy.", ERRSTR_LEN);
		else
			strncpy(svc_errinfo, "[recv]", ERRSTR_LEN);
		if (NULL != rate) {
			rate->transmitted = -1;
		}
		return -1;
	}

	int resp_code = atoi(resp.code);
	if (RESP_DUPLICATED == resp_code) {
		strncpy(svc_errinfo, "Duplicated file name.", ERRSTR_LEN);
		return -1;
	} else if (RESP_OUT_OF_DISK == resp_code) {
		strncpy(svc_errinfo, "Server out of disk space.", ERRSTR_LEN);
		return -1;
	} else if (RESP_OUT_OF_MEMORY == resp_code) {
		strncpy(svc_errinfo, "Server out of memory.", ERRSTR_LEN);
		return -1;
	}
	if (RESP_OK != resp_code) {
		strncpy(svc_errinfo, "[upload_service] Unknown error.", ERRSTR_LEN);
		return -1;
	}

	// Send file data if the server said OK.
	if (send_file(sockfd, path, flen, rate) < 0)
		return -1;

	return 0;
}

#endif // _CLIENT_H_

#ifdef _SERVER_APPLICATION_
extern struct inventory g_inven_cache;

/*
 * 1: File exists.
 * 0: No such file exists.
 */
static int
check_file_exists(const char *fname)
{
	return (NULL != find(g_inven_cache.nametb, fname));
}

static void
set_resp_type(struct svc_resp *resp, enum SERVICE_TYPE type)
{
	snprintf(resp->type, SVC_TYPE_LEN, "%d", type);
}

static void
set_resp_code(struct svc_resp *resp, enum RESPONSE_CODE code)
{
	snprintf(resp->code, RESP_CODE_LEN, "%d", code);
}

int 
server_upload_service(int clsock)
{
	struct svc_req req;
	struct svc_resp resp;
	set_resp_type(&resp, SVC_UPLOAD);

	// Receive svc_req
	if (recv(clsock, &req, sizeof(struct svc_req), 0) < 0) {
		printf("[handle_request] [recv] client %d\n",
				clsock);
		return -1;
	}

	/* TODO
	 * 중복 검사, 서버 용량 검사
	 */
	int64_t flen = strtoll(req.flen, NULL, 10);
	enum ACCESS_LEVEL alv = atoi(req.alv);

	printf("[client %d request] [%s %udB (%d)]\n",
			clsock, req.fname, flen, alv);

	void *buf = calloc(flen, 1);
	if (NULL == buf) {
		printf("[server_upload_service] [malloc]\n");
		set_resp_code(&resp, RESP_OK);
		return -1;
	}

	// Send response
	snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_OK);
	if (send(clsock, &resp, sizeof(struct svc_resp), 0) < 0) {
		printf("[server_upload_service] [send]");
		return -1;
	}


	// Receive file data.
	ssize_t rlen = 0;
	int cnt = 0;
	while (rlen < flen) {
		cnt++;
		ssize_t chunk = recv(clsock, buf + rlen, flen - rlen, 0);
		if (0 == chunk){
			printf("client closed\n");
			break;
		} else if (chunk < 0) {
			fprintf(stderr, "[server_upload_service] [recv] %s\n", strerror(errno));
			return -1;
		}
		rlen += (ssize_t)chunk;
	}
	if (rlen == flen)
		printf("Upload complete(%d). (%ldB/%ldB)\n", cnt, rlen, flen);
	else
		printf("Upload incomplete(%d). (%ldB/%ldB)\n", cnt, rlen, flen);
	free(buf);
	
	return 0;
}

#endif // _SERVER_APPLICATION_
