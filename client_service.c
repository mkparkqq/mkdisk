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
#include <fcntl.h>
#include <sys/mman.h>

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
	snprintf(req.flen, REQ_FLEN_LEN, "%ld", flen);
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

	printf("\033[2K\033[GWait...");
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

	/*
	 * 서버가 파일을 받은 뒤 파일을 디스크에 내리린다.
	 * 서버가 다운되지 않는 이상 디스크에 내리는 작업의 성공 여부를
	 * 반환하기 때문에 계속 기다린다.
	 *
	 * timeout을 설정할 경우 아래와 같은 문제를 해결해야 한다.
	 * timeout으로 tx_failed로 갔는데 서버가 그 뒤에 응답을 보낼 경우
	 * 클라이언트는 다음 서비스에서 그 응답을 읽으면서 프로토콜이 깨지게 된다.
	 * 사실 위의 모든 tx_failed가 그렇다...
	 * g_servsock을 닫았다가 다시 여는 방법으로 해결할 수 있다.
	 */
	if (set_socket_timeout(sockfd, 0) < 0) {
		strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
		goto tx_failed;
	}

	printf("\033[2K\033[GServer processing...");
	fflush(stdout);

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
	g_client_status.ltx = TX_FAILED;
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
	} else 
		memset(g_items, 0x00, g_client_status.dcontent.item_num * sizeof(struct inven_item));

	printf("\033[2K\033[GDownloading file inventory...");
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
	g_client_status.ltx = TX_FAILED;
	return -1;
}


int 
client_download_service(int sockfd, struct inven_item *item, 
		struct trans_stat *rate)
{
	struct svc_req req;
	struct svc_resp resp;
	int result = 0;
	int64_t flen = 0;
	int fd = -1;
	void *map = NULL;
	char downloadpath[FILE_NAME_LEN + DOWNLOAD_HOME_LEN];
	
	// Send download request.
	snprintf(req.type, SVC_TYPE_LEN, "%d", SVC_DOWNLOAD);
	strncpy(req.fname, item->fname, FILE_NAME_LEN);
	result = send_stream(sockfd, &req, sizeof(struct svc_req));

	if (set_socket_timeout(sockfd, SERVER_RESP_TIMEOUT) < 0) {
		strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
		goto tx_failed;
	}

	// Receive response
	if (recv(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno) 
			strncpy(svc_errinfo, "Server is busy.", ERRSTR_LEN);
		else
			strncpy(svc_errinfo, "[recv]", ERRSTR_LEN);
		goto tx_failed;
	}
	
	if (RESP_OK == atoi(resp.code)) {
		flen = strtoll(item->flen, NULL, 10);

		if (set_socket_timeout(sockfd, 0) < 0) {
			strncpy(svc_errinfo, "[set_socket_timeout]", ERRSTR_LEN);
			goto tx_failed;
		}

		// Create file size flen.
		snprintf(downloadpath, DOWNLOAD_HOME_LEN + FILE_NAME_LEN + 1, 
				"%s/%s", DOWNLOAD_HOME_STR, item->fname);
		fd = open(downloadpath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			strncpy(svc_errinfo, "Failed to create a new file(1).", ERRSTR_LEN);
			goto tx_failed;
		}
		if (ftruncate(fd, flen) < 0) {
			strncpy(svc_errinfo, "Failed to create a new file(2).", ERRSTR_LEN);
			goto tx_failed;
		}
		// Map the file to the buffer.
		map = mmap(NULL, flen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (MAP_FAILED == map) {
			strncpy(svc_errinfo, "Failed to create a new file(3).", ERRSTR_LEN);
			goto tx_failed;
		}

		// Receive the file data.
		result = recv_stream_nblock(sockfd, map, flen, rate);
		if (result < 0) {
			strncpy(svc_errinfo, sockutil_errstr(result), ERRSTR_LEN);
			goto tx_failed;
		}

		if (munmap(map, flen) < 0) {
			strncpy(svc_errinfo, "Failed to create a new file(4).", ERRSTR_LEN);
			goto tx_failed;
		}

		return 0;
	} else if (RESP_NO_SUCH_FILE == atoi(resp.code)) {
			strncpy(svc_errinfo, "The file could not be found.", ERRSTR_LEN);
			goto svc_refused;
	} else if (RESP_ACCESS_DENIED == atoi(resp.code)) {
			strncpy(svc_errinfo, "Access denied.", ERRSTR_LEN);
			goto svc_refused;
	}

tx_failed:
	g_client_status.ltx = TX_FAILED;
svc_refused:
	if (NULL != map) {
		msync(map, flen, MS_INVALIDATE);
		munmap(map, flen);
	}
	if (-1 != fd)
		close(fd);
	if (NULL != rate)
		rate->transmitted = -1;
	return -1;
}
