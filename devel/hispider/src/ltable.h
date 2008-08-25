#include "dnsdb.h"
#ifndef _LTABLE_H
#define _LTABLE_H
#define DOC_KEY_LEN 16
#define LTABLE_PATH_MAX     256
#define HTTP_BUF_SIZE  65536
#ifndef HTTP_HOST_MAX
#define HTTP_HOST_MAX  64
#endif
#ifndef HTTP_PATH_MAX
#define HTTP_PATH_MAX 960
#endif
#ifndef HTTP_URL_MAX
#define HTTP_URL_MAX  1024
#endif
#define TASK_STATE_INIT         0x00
#define TASK_STATE_OK           0x02
#define TASK_STATE_ERROR        0x04
#define HTTP_DOWNLOAD_TIMEOUT   10000000
#define HTML_MAX_SIZE           2097152

#define LTABLE_META_NAME    "hispider.meta"
#define LTABLE_URL_NAME     "hispider.url"
#define LTABLE_DOC_NAME     "hispider.doc"
#define LTABLE_STATE_NAME   "hispider.state"
#define USER_AGENT "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; zh-CN; rv:1.9.0.1) Gecko/2008070206 Firefox/3.0.1"
#define ACCEPT_TYPE          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
#define ACCEPT_LANGUAGE     "zh-cn,zh;q=0.5"
#define ACCEPT_ENCODING     "gzip,deflate"
#define ACCEPT_CHARSET      "gb2312,utf-8;q=0.7,*;q=0.7"
#define HTTP_RESP_OK        "HTTP/1.0 200 OK"
#define HTTP_BAD_REQUEST    "HTTP/1.0 404 Not Found\r\n\r\n"
typedef long long LLong;
typedef struct _LMETA
{
    int state;
    int date;
    off_t offset;
    int length;
    off_t offurl;
    int nurl;
    unsigned char id[DOC_KEY_LEN];
}LMETA;
typedef struct _LHEADER
{
    int ndate;
    int nurl;
    int nzdata;
    int ndata;
}LHEADER;
typedef struct _LSTATE
{
    int taskid;
    int lastid;
}LSTATE;
typedef struct _LTABLE
{
    void *mutex;
    DNSDB *dnsdb;
    void *urltable;
    void *logger;
    int isinsidelogger;
    int url_fd;
    int meta_fd;
    int doc_fd;
    int state_fd;
    long url_current;
    long url_total;
    long url_ok;
    long url_error;
    LLong doc_size;
    LLong doc_zsize;

    int     (*set_basedir)(struct _LTABLE *, char *basedir);
    int     (*resume)(struct _LTABLE *);
    int     (*parselink)(struct _LTABLE *, char *host, char *path, 
            char *content, char *end);
    int     (*addlink)(struct _LTABLE *, unsigned char *host, 
            unsigned char *path, unsigned char *href, unsigned char *ehref);
    int     (*addurl)(struct _LTABLE *, char *host, char *path);
    int     (*get_task)(struct _LTABLE *, char *block, long *nblock);
    int     (*set_task_state)(struct _LTABLE *, int taskid, int state);
    int     (*get_stateinfo)(struct _LTABLE *, char *block);
    int     (*add_document)(struct _LTABLE *, int taskid, int date, char *content, int ncontent);
    int     (*get_dns_task)(struct _LTABLE *, char *host);
    int     (*over_dns_task)(struct _LTABLE *, int dns_taskid, char *host, int ip);
    void    (*clean)(struct _LTABLE **);
}LTABLE;
/* initialize LTABLE */
LTABLE *ltable_init();
#endif
