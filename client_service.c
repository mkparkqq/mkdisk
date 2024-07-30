#include "client.h"
#include "module/service.h"
#include "module/timeutil.h"
#include "module/hashmap.h"
#include "module/queue.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERVER_RESP_TIMEOUT		5

extern struct client_status g_client_status;
extern struct inven_item *g_items;
char svc_errinfo[ERRSTR_LEN];

static int
send_svc_req(int sockfd, const char *path, int64_t flen,  enum ACCESS_LEVEL alv, enum SERVICE_TYPE type)
{
	struct svc_req req;
	memset(&req, 0x00, sizeof(struct svc_req));

	if (SVC_INQUIRY == type)
		goto inquiry_req;

	int64_t slen = 0;
	char *fname = strrchr(path, '/');
	if (NULL == fname)
		fname = path;
	else
		fname += sizeof(char);

	strncpy(req.fname, fname, FILE_NAME_LEN);
	snprintf(req.flen, REQ_FLEN_LEN, "%d", flen);
	snprintf(req.alv, REQ_ALV_LEN, "%d", alv);
inquiry_req:
	snprintf(req.type, SVC_TYPE_LEN, "%d", type);

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

	slen = send_stream_nblock(sockfd, data, flen, rate);
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
	if (send_svc_req(sockfd, path, flen, alv, SVC_UPLOAD) < 0) {
		strncpy(svc_errinfo, "[send_svc_req]", ERRSTR_LEN);
		goto tx_failed;
	}

	if (set_socket_timeout(sockfd, SERVER_RESP_TIMEOUT) < 0) {
		strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
		goto tx_failed;
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
		goto tx_failed;
	}

	int resp_code = atoi(resp.code);
	if (RESP_DUPLICATED == resp_code) {
		strncpy(svc_errinfo, "File name already exists.", ERRSTR_LEN);
		goto request_refused;
	} else if (RESP_OUT_OF_DISK == resp_code) {
		strncpy(svc_errinfo, "Server out of disk space.", ERRSTR_LEN);
		goto request_refused;
	} else if (RESP_OUT_OF_MEMORY == resp_code) {
		strncpy(svc_errinfo, "Server out of memory.", ERRSTR_LEN);
		goto request_refused;
	} else if (RESP_INVENTORY_FULL == resp_code) {
		strncpy(svc_errinfo, "Server inventory is full", ERRSTR_LEN);
		goto request_refused;
	}
	if (RESP_OK != resp_code) {
		snprintf(svc_errinfo, ERRSTR_LEN, 
				"[upload_service] Unknown error(%d).", resp_code);
		goto request_refused;
	}

	if (send_file(sockfd, path, flen, rate) < 0)
		goto tx_failed;

	// Check if the server successfully saved the file.
	if (recv(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno) 
			strncpy(svc_errinfo, "The server failed to save the file. Transaction rolled back", ERRSTR_LEN);
		else
			strncpy(svc_errinfo, "[recv]", ERRSTR_LEN);
		goto tx_failed;
	}

	resp_code = atoi(resp.code);
	if (RESP_OK == resp_code)
		return 0;
	else if (RESP_OUT_OF_DISK == resp_code) {
		strncpy(svc_errinfo, "Server out of disk space. Transaction rolled back.", ERRSTR_LEN);
		goto request_refused;
	} else {
		snprintf(svc_errinfo, ERRSTR_LEN, 
				"[upload_service] Unknown error(%d). Transaction rolled back.", resp_code);
		goto request_refused;
	}

request_refused:
tx_failed:
	if (NULL != rate) {
		rate->transmitted = -1;
	}
	return -1;
}

int
client_inquiry_service(int sockfd, struct trans_stat *rate)
{
	int result = 0;
	int64_t dlen = 0;

	// Send svc_req.
	if (send_svc_req(sockfd, NULL, 0, 0, SVC_INQUIRY) < 0) {
		strncpy(svc_errinfo, "[send_svc_req]", ERRSTR_LEN);
		goto tx_failed;
	}

	if (set_socket_timeout(sockfd, SERVER_RESP_TIMEOUT) < 0) {
		strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
		goto tx_failed;
	}

	// Receive svc_resp
	struct svc_resp resp;
	if (recv(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno) 
			strncpy(svc_errinfo, "Server is busy.", ERRSTR_LEN);
		else
			strncpy(svc_errinfo, "[recv]", ERRSTR_LEN);
		goto tx_failed;
	}

	dlen = strtoll(resp.code, NULL, 10);
	if (NULL == g_items) {
		g_items = (struct inven_item *) malloc(dlen);
		if (NULL == g_items){
			strncpy(svc_errinfo, "Out of memory.", ERRSTR_LEN);
			goto tx_failed;
		}
	}

	printf("\033[2KDownloading file inventory...");
	fflush(stdout);

	if (recv_stream_nblock(sockfd, g_items, dlen, rate) < 0) {
		strncpy(svc_errinfo, sockutil_errstr(result), ERRSTR_LEN);
		goto tx_failed;
	}

	g_client_status.dcontent.item_num = dlen / sizeof(struct inven_item);

	return 0;

tx_failed:
	if (NULL != rate)
		rate->transmitted = -1;
	return -1;
}
