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

extern struct inventory g_inventory;

/*
 * 1: File exists.
 * 0: No such file exists.
 */
static int
check_file_exists(const char *fname)
{
	if(NULL == find(g_inventory.nametb, fname))
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
rollback_inventory(int* fid, struct svc_req *req)
{
	snprintf(g_inventory.items[*fid].status, sizeof(g_inventory.items[*fid].status), "%d", ITEM_STAT_DELETED);
	enqueue(g_inventory.fidq, (void *)fid);
	rm_item(g_inventory.nametb, req->fname);
	free(fid);
}

int 
server_upload_service(int clsock, struct svc_req *req)
{
	int result = 0;
	char tstamp_ms[TIMESTAMP_MS_LEN];
	char tstamp[TIMESTAMP_LEN];
	struct svc_resp resp;
	set_resp_type(&resp, SVC_UPLOAD);

	int64_t flen = strtoll(req->flen, NULL, 10);
	enum ACCESS_LEVEL alv = atoi(req->alv);
	int *fid = (int *)malloc(sizeof(int));
	*fid = -1;

	timestamp(MSEC, "[request] [client (%d)] %s %uB (%d)", 
			clsock, req->fname, flen, alv);

	// 서버 메모리 확인
	void *buf = NULL;
	buf = malloc(flen);
	if (NULL == buf) {
		timestamp(MSEC, "[server_upload_service] [refuse] [client (%d)] [malloc]", clsock);
		set_resp_code(&resp, RESP_OUT_OF_MEMORY);
		goto refuse_svc;
	}
	// 파일 이름 사용 가능하면 일단 nametb 선점
	if (set(g_inventory.nametb, req->fname, (void *)fid, 0) < 0) {
		timestamp(MSEC, "[server_upload_service] [refuse] file already exists(%s)", req->fname);
		set_resp_code(&resp, RESP_DUPLICATED);
		goto refuse_svc;
	}
	// g_inventory.items에 빈 공간이 있는지 확인
	if (dequeue(g_inventory.fidq, fid) < 0) {
		timestamp(MSEC, "[server_upload_service] [refuse] fidq");
		rm_item(g_inventory.nametb, req->fname); // rollback
		set_resp_code(&resp, RESP_INVENTORY_FULL);
		goto refuse_svc;
	}
	set(g_inventory.nametb, req->fname, (void *)fid, 1);
	// TODO 서버 disk  용량 검사

	// Send response
	snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_OK);
	if (send(clsock, &resp, sizeof(struct svc_resp), 0) < 0) {
		timestamp(MSEC, "[server_upload_service] [send]");
		return -1;
	}

	// g_inventory.items 배열 업데이트 (commit)
	char clientip[IP_ADDRESS_LEN];
	char ts[TIMESTAMP_LEN];
	tstamp_sec(ts, TIMESTAMP_LEN);
	get_client_ipaddr(clsock, clientip, IP_ADDRESS_LEN);
	strncpy(g_inventory.items[*fid].creator, clientip, sizeof(g_inventory.items[*fid].creator));
	strncpy(g_inventory.items[*fid].fname, req->fname, sizeof(g_inventory.items[*fid].fname));
	strncpy(g_inventory.items[*fid].alv, req->alv, sizeof(g_inventory.items[*fid].alv));
	strncpy(g_inventory.items[*fid].last_modified, ts, sizeof(g_inventory.items[*fid].last_modified));
	snprintf(g_inventory.items[*fid].flen, sizeof(g_inventory.items[*fid].flen), "%s", req->flen);
	
	// Receive file data.
	ssize_t rlen = 0;
	int cnt = 0;
	while (rlen < flen) {
		cnt++;
		ssize_t chunk = recv(clsock, buf + rlen, flen - rlen, 0);
		if (0 == chunk){
			timestamp(MSEC, "[client (%d)] socket closed\n", clsock);
			break;
		} else if (chunk < 0) {
			rollback_inventory(fid, req);
			timestamp(MSEC, "[server_upload_service] [recv] [client (%d)] %s", clsock, strerror(errno));
			return -1;
		}
		rlen += (ssize_t)chunk;
	}
	if (rlen == flen)
		timestamp(MSEC, "[client (%d)] Transmission complete(%d). (%ldB/%ldB)", 
				clsock, cnt, rlen, flen);
	// Transmission failed. Rollback g_inventory.
	else {
		rollback_inventory(fid, req);
		timestamp(MSEC, "[server_upload_service] [client (%d)] partially transmitted (%d/%d)", clsock, rlen, flen);
		free(buf);
		return -1;
	}

	// Create new file
	if (create_directory_if_not_exists(g_inventory.items[*fid].creator) < 0) {
		rollback_inventory(fid, req);
		timestamp(MSEC, "[client (%d)] Failed to create new directory.", clsock);
		goto disk_failure;
	}
	char fpath[FS_PATH_MAX_LEN];
	memset(fpath, '\0', FS_PATH_MAX_LEN);
	snprintf(fpath, FS_PATH_MAX_LEN, "%s/%s",
			g_inventory.items[*fid].creator, req->fname);

	result = create_file(fpath, buf, flen);
	if (result < 0) {
		rollback_inventory(fid, req);
		timestamp(MSEC, "[client (%d)] [create_file] Failed to create new file. %s", clsock, futil_errstr(result));
		goto disk_failure;
	}

	snprintf(g_inventory.items[*fid].status, sizeof(g_inventory.items[*fid].status), "%d", ITEM_STAT_AVAILABLE);

	timestamp(MSEC, "[client (%d)] Finished to create the file.", clsock);

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

// client_service에도 존재. -> 공통 모듈로 분리
static int
send_file(int sockfd, const char *path, int64_t flen, struct trans_stat *rate)
{
	int64_t dlen = 0;// Size of data to send in bytes.
	int64_t slen = 0;// Size of data sent in bytes.

	void *data = malloc(flen);
	if (NULL == data) {
		timestamp(MSEC, "[send_file] [malloc]");
		 return -1;
	}

	dlen = read_file(path, data, flen);
	if (dlen < 0) {
		timestamp(MSEC, "[send_file] [read_file]");
		goto futil_err;
	}

	slen = send_stream_nblock(sockfd, data, flen, rate);
	if (slen < 0) {
		timestamp(MSEC, "[send_file] %s", sockutil_errstr(slen));
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

int 
server_download_service(int sockfd, struct svc_req *req)
{
	timestamp(MSEC, "[server_download_service] [client (%d)] [%s]",
			sockfd, req->fname);
	struct svc_resp resp;
	char fpath[IP_ADDRESS_LEN + FILE_NAME_LEN];
	int64_t flen = 0;
	char clip[IP_ADDRESS_LEN];
	int *fid = NULL;
	int alv = -1;
	int result = 0;

	get_client_ipaddr(sockfd, clip, IP_ADDRESS_LEN);
	fid = (int *) find(g_inventory.nametb, req->fname);

	// Check if the file is deleted.
	if (NULL == fid) {
		snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_DELETED);
		goto refuse_svc;
	}

	alv = atoi(g_inventory.items[*fid].alv);
	flen = strtoll(g_inventory.items[*fid].flen, NULL, 10);

	// Check access level.
	if ((alv == PRIVATE_ACCESS) && (strcmp(clip, g_inventory.items[*fid].creator))) {
		snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_ACCESS_DENIED);
		goto refuse_svc;
	}

	if (0 == pthread_rwlock_tryrdlock(&g_inventory.ilock[*fid])) {
		snprintf(fpath, IP_ADDRESS_LEN + FILE_NAME_LEN, "%s/%s",
				g_inventory.items[*fid].creator, req->fname);
		// Check file exists.
		if (access(fpath, F_OK) < 0) {
			timestamp(MSEC, "[server_download_service] [client (%d)] [Miss (%s)]", sockfd, fpath);
			snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_NO_SUCH_FILE);
			pthread_rwlock_unlock(&g_inventory.ilock[*fid]);
			goto refuse_svc;
		}
		// Send OK response.
		timestamp(MSEC, "[server_download_service] [client (%d)] OK",
				sockfd);
		snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_OK);
		result = send_stream(sockfd, &resp, sizeof(struct svc_resp));
		if(result < 0) {
			timestamp(MSEC, "%s", sockutil_errstr(result));
			pthread_rwlock_unlock(&g_inventory.ilock[*fid]);
			return -1;
		}
		// Send the file.
		if (send_file(sockfd, fpath, flen, NULL) < 0) {
			timestamp(MSEC, "%s", sockutil_errstr(result));
			pthread_rwlock_unlock(&g_inventory.ilock[*fid]);
			timestamp(MSEC, "[server_download_service] [client (%d)] Download successed.",
				sockfd);
			return -1;
		}
		timestamp(MSEC, "[server_download_service] [client (%d)] File sended.", sockfd);
		pthread_rwlock_unlock(&g_inventory.ilock[*fid]);
		return 0;
	} else {
		snprintf(resp.code, RESP_CODE_LEN, "%d", RESP_MODIFYING);
		goto refuse_svc;
	}

refuse_svc:
	if(send_stream(sockfd, &resp, sizeof(struct svc_resp)) < 0) {
		timestamp(MSEC, "%s", sockutil_errstr(result));
		return -1;
	}
	return 0;
}

int 
server_inquiry_service(int sockfd, size_t max_item, struct svc_req *req)
{
	struct svc_resp resp;
	int result = 0;
	int dlen = max_item * sizeof(struct inven_item);

	// Send data size.
	set_resp_type(&resp, SVC_INQUIRY);
	snprintf(resp.code, RESP_CODE_LEN, "%d", dlen);
	if (send(sockfd, &resp, sizeof(struct svc_resp), 0) < 0) {
		timestamp(MSEC, "[server_upload_service] [send]");
		return -1;
	}

	if(send_stream(sockfd, g_inventory.items, dlen) < 0) {
		timestamp(MSEC, sockutil_errstr(result), result, dlen);
		return -1;
	}

	timestamp(MSEC, "[server_inquiry_service] Successed(%lluB).", dlen);

	return 0;
}

int 
server_rename_service(int sockfd, struct svc_req *req)
{
	;
}

int 
server_delete_service(int sockfd, struct svc_req *req)
{
	;
}
