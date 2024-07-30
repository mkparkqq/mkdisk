#include "server.h"
#include "module/service.h"
#include "module/timeutil.h"
#include "module/hashmap.h"
#include "module/queue.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define FILE_EXISTS		1
#define NO_SUCH_FILE	0
#define FS_PATH_MAX_LEN	256

extern struct inventory g_inven_cache;

/*
 * 1: File exists.
 * 0: No such file exists.
 */
static int
check_file_exists(const char *fname)
{
	if(NULL == find(g_inven_cache.nametb, fname))
		return NO_SUCH_FILE;
	return FILE_EXISTS;
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
get_client_ipaddr(int sockfd, char *buf, size_t buflen)
{
	strncpy(buf, "Unkown host", buflen);
	struct sockaddr_in claddr;
    socklen_t addr_len = sizeof(claddr);
    char ip_str[IP_ADDRESS_LEN];

    // Get the client's address.
    if (getpeername(sockfd, (struct sockaddr *)&claddr, &addr_len) == -1) {
        return -1;
    }

	// IP address -> string
    if (inet_ntop(AF_INET, &claddr.sin_addr, ip_str, sizeof(ip_str)) == NULL) {
        return -1;
    }

    strncpy(buf, ip_str, buflen - 1);
    buf[buflen - 1] = '\0';

    return 0;
}

static void	
rollback_inven_cache(int* fid, struct svc_req *req)
{
	snprintf(g_inven_cache.items[*fid].status, sizeof(g_inven_cache.items[*fid].status), "%d", ITEM_STAT_DELETED);
	enqueue(g_inven_cache.fidq, (void *)fid);
	rm_item(g_inven_cache.nametb, req->fname);
	free(fid);
}

int 
server_upload_service(int clsock, struct svc_req *req)
{
	char tstamp_ms[TIMESTAMP_MS_LEN];
	char tstamp[TIMESTAMP_LEN];
	struct svc_resp resp;
	set_resp_type(&resp, SVC_UPLOAD);

	int64_t flen = strtoll(req->flen, NULL, 10);
	enum ACCESS_LEVEL alv = atoi(req->alv);
	int *fid = (int *)malloc(sizeof(int));
	*fid = -1;

	timestamp(MSEC, "[client %d request] [%s %uB (%d)]", clsock, req->fname, flen, alv);

	// 서버 메모리 확인
	void *buf = NULL;
	buf = malloc(flen);
	if (NULL == buf) {
		timestamp(MSEC, "[server_upload_service] [malloc]");
		set_resp_code(&resp, RESP_OUT_OF_MEMORY);
		goto refuse_svc;
	}
	// 파일 이름 사용 가능하면 일단 nametb 선점
	if (set(g_inven_cache.nametb, req->fname, (void *)fid, 0) < 0) {
		timestamp(MSEC, "[server_upload_service] file already exists(%s)", req->fname);
		set_resp_code(&resp, RESP_DUPLICATED);
		goto refuse_svc;
	}
	// g_inven_cache.items에 빈 공간이 있는지 확인
	if (dequeue(g_inven_cache.fidq, fid) < 0) {
		rm_item(g_inven_cache.nametb, req->fname); // rollback
		set_resp_code(&resp, RESP_INVENTORY_FULL);
		goto refuse_svc;
	}
	set(g_inven_cache.nametb, req->fname, (void *)fid, 1);
	// TODO 서버 용량 검사

	// Send response
	snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_OK);
	if (send(clsock, &resp, sizeof(struct svc_resp), 0) < 0) {
		timestamp(MSEC, "[server_upload_service] [send]");
		return -1;
	}

	// g_inven_cache.items 배열 업데이트 (commit)
	char clientip[IP_ADDRESS_LEN];
	char ts[TIMESTAMP_LEN];
	tstamp_sec(ts, TIMESTAMP_LEN);
	get_client_ipaddr(clsock, clientip, IP_ADDRESS_LEN);
	strncpy(g_inven_cache.items[*fid].creator, clientip, sizeof(g_inven_cache.items[*fid].creator));
	strncpy(g_inven_cache.items[*fid].fname, req->fname, sizeof(g_inven_cache.items[*fid].fname));
	strncpy(g_inven_cache.items[*fid].alv, req->alv, sizeof(g_inven_cache.items[*fid].alv));
	strncpy(g_inven_cache.items[*fid].last_modified, ts, sizeof(g_inven_cache.items[*fid].last_modified));
	snprintf(g_inven_cache.items[*fid].status, sizeof(g_inven_cache.items[*fid].status), "%d", ITEM_STAT_AVAILABLE);
	
	// Receive file data.
	ssize_t rlen = 0;
	int cnt = 0;
	while (rlen < flen) {
		cnt++;
		ssize_t chunk = recv(clsock, buf + rlen, flen - rlen, 0);
		if (0 == chunk){
			timestamp(MSEC, "client closed\n");
			break;
		} else if (chunk < 0) {
			rollback_inven_cache(fid, req);
			timestamp(MSEC, "[server_upload_service] [recv] %s", strerror(errno));
			return -1;
		}
		rlen += (ssize_t)chunk;
	}
	if (rlen == flen)
		timestamp(MSEC, "Upload complete(%d). (%ldB/%ldB)", cnt, rlen, flen);
	// Transmission failed. Rollback g_inven_cache.
	else {
		rollback_inven_cache(fid, req);
		timestamp(MSEC, "Upload incomplete(%d). (%ldB/%ldB)", cnt, rlen, flen);
		free(buf);
		return -1;
	}

	// Create new file
	if (create_directory_if_not_exists(g_inven_cache.items[*fid].creator) < 0) {
		rollback_inven_cache(fid, req);
		timestamp(MSEC, "Failed to create new directory.");
		goto disk_failure;
	}
	char fpath[FS_PATH_MAX_LEN];
	memset(fpath, '\0', FS_PATH_MAX_LEN);
	snprintf(fpath, FS_PATH_MAX_LEN, "%s/%s",
			g_inven_cache.items[*fid].creator, req->fname);

	if (create_file(fpath, buf, flen) < 0) {
		rollback_inven_cache(fid, req);
		timestamp(MSEC, "Failed to create new file.");
		goto disk_failure;
	}
	timestamp(MSEC, "Finished to create the file.");

	set_resp_code(&resp, RESP_OK);

	if (send(clsock, &resp, sizeof(struct svc_resp), 0) < 0)
		timestamp(MSEC, "[server_upload_service] [send]");
	
	free(buf);

	return 0;

disk_failure:
	set_resp_code(&resp, RESP_OUT_OF_DISK);
refuse_svc:
	if (send(clsock, &resp, sizeof(struct svc_resp), 0) < 0)
		timestamp(MSEC, "[server_upload_service] [send]");
	if (NULL != buf)
		free(buf);
	return -1;
}


int 
server_download_service(int sockfd)
{

}

int 
server_inquiry_service(int sockfd, size_t max_item, struct svc_req *req)
{
	struct svc_resp resp;
	int result = 0;
	int dlen = max_item * sizeof(struct inven_item);

	/*
	// Receive svc_req.
	if (recv(sockfd, req, sizeof(struct svc_req), 0) < 0) {
		timestamp(MSEC, "[%s] [handle_request] [recv] client %d", sockfd);
		return -1;
	}
	*/

	// Send data size.
	set_resp_type(&resp, SVC_INQUIRY);
	snprintf(resp.code, RESP_CODE_LEN, "%llu", dlen);
	if (send(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		timestamp(MSEC, "[server_upload_service] [send]");
		return -1;
	}

	if(send_stream(sockfd, g_inven_cache.items, dlen) < 0) {
		timestamp(MSEC, sockutil_errstr(result), result, dlen);
		return -1;
	}

	timestamp(MSEC, "[server_inquiry_service] Successed(%lluB).", dlen);

	return 0;
}

int 
server_rename_service(int sockfd)
{
	;
}

int 
server_delete_service(int sockfd)
{
	;
}
