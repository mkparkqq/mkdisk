#ifndef _SERVICE_H_
#define _SERVICE_H_

#include "fileutil.h"
#include "sockutil.h"

#include <stdint.h>

#define FILE_NAME_LEN	 		32
#define IP_ADDRESS_LEN			INET_ADDRSTRLEN
#define PORTNO_LEN				6
#define TIMESTAMP_LEN			20
#define TIMESTAMP_MS_LEN		24
#define ERRSTR_LEN 				64
#define SVC_TYPE_LEN			2
#define REQ_ALV_LEN				2
#define REQ_FLEN_LEN			20
#define RESP_CODE_LEN			20

enum SERVICE_TYPE {
	SVC_UPLOAD = 0,
	SVC_DOWNLOAD,
	SVC_RENAME,
	SVC_DELETE,
	SVC_INQUIRY,
	SVC_NUM
};

enum RESPONSE_CODE {
	RESP_OK = 1,
	RESP_DUPLICATED,
	RESP_OUT_OF_MEMORY,
	RESP_OUT_OF_DISK,
	RESP_INVENTORY_FULL,
	RESP_PAHNTOM_ITEM,
	RESP_UNDEFINED_SVC
	// TODO
};

enum ACCESS_LEVEL {
	PUBLIC_ACCES = 0,
	PRIVATE_ACCESS
};

enum ITEM_STAT {
	ITEM_STAT_AVAILABLE,
	ITEM_STAT_DELETED,
	ITEM_STAT_DELETING,
	ITEM_STAT_MODIFYING
};

struct svc_resp {
	// enum SERVICE_TYPE svc_type;
	char type[SVC_TYPE_LEN];
	enum RESPONSE_CODE resp_code;
	char code[RESP_CODE_LEN];
};

struct svc_req {
	// enum SERVICE_TYPE svc_type;
	char type[SVC_TYPE_LEN];
	char fname[FILE_NAME_LEN];
	// int64_t flen;
	char flen[REQ_FLEN_LEN];
	//enum ACCESS_LEVEL alv;
	char alv[REQ_ALV_LEN];
};

struct inven_item {
	char creator[IP_ADDRESS_LEN];
	char fname[FILE_NAME_LEN];
	char alv[2];
	char last_modified[TIMESTAMP_LEN];
	// enum ITEM_STAT status;
	char status[2];
};

/*
 * Not thread-safe.
 * Just for the client.
 */
char *svc_errstr(void);
int client_upload_service(int, const char *, int64_t, enum ACCESS_LEVEL, struct trans_stat *);
int client_inquiry_service(int, struct trans_stat *);
int client_download_service(int, struct trans_stat *);

/*
 * Just for the server.
 */
// TODO
int server_upload_service(int, struct svc_req *);
int server_download_service(int);
int server_inquiry_service(int, size_t, struct svc_req *);
int server_rename_service(int);
int server_delete_service(int);

#endif // _SERVICE_H_
