#ifndef _LTASK_H
#define _LTASK_H
#define L_PATH_MAX      256
#define L_URL_MAX       4096
#define HOST_INCRE_NUM  1000000   
#define PROXY_INCRE_NUM 10000
#define URL_INCRE_NUM   1000000   
#define IP_INCRE_NUM    1000000
#define QUEUE_INCRE_NUM 100000
#define L_STATE_NAME    "hi.state"
#define L_URL_NAME      "hi.url"
#define L_PROXY_NAME    "hi.proxy"
#define L_HOST_NAME     "hi.host"
#define L_IP_NAME       "hi.ip"
#define L_DOMAIN_NAME   "hi.domain"
#define L_DOC_NAME      "hi.doc"
#define L_QUEUE_NAME    "hi.queue"
#define L_LOG_NAME      "hi.log"
/* host/domain */
typedef struct _LHOST
{
    int host_off;
    int host_len;
    int ip_off;
    short ip_count;
    short status;
    int ntimeout;
    int url_first_id;
    int url_current_id;
    int url_total;
    int url_left;
}LHOST;
/* proxy */
typedef struct _LPROXY
{
    int ip;
    short port;
    short status;
    int nfailure;
}LPROXY;
/* url/doc meta */
typedef struct _LMETA
{
    short status;
    short type;
    int date;
    int url_off;
    int url_len;
    int host_id;
    off_t content_off;
    int content_len;
    int prev;
    int next;
}LMETA;
/* state */
typedef struct _LSTATE
{
    short running;
    short url_queue_type;
    int   url_total;
    off_t document_size_total;
    int   speed;
}LSTATE;
/* IO/MAP */
typedef struct _LIO
{
    int fd;
    void *map;
    off_t size;
    off_t end;
    int total;
    int current;
    int left;
}LIO;
/* PRIORITY QUEUE */
typedef struct _LNODE
{
    short type;
    short status;
    int id;
}LNODE;
/* TASK */
typedef struct _LTASK
{
    LIO  proxyio;
    LIO  hostio;
    LIO  ipio;
    LIO  queueio;
    int  url_fd;
    int  domain_fd;
    int  doc_fd;
    void *urlmap;
    void *table;
    LSTATE *state;
    int state_fd;
    void *timer;
    void *mutex;
    void *logger;

    int (*set_basedir)(struct _LTASK *, char *basedir);
    int (*add_proxy)(struct _LTASK *, char *ip, char *port);
    int (*get_proxy)(struct _LTASK *, LPROXY *proxy);
    int (*del_proxy)(struct _LTASK *, int proxy_id);
    int (*add_host)(struct _LTASK *, char *host);
    int (*pop_host)(struct _LTASK *, char *host);
    int (*set_host_status)(struct _LTASK *, char *host, short status);
    int (*set_host_priority)(struct _LTASK *, char *host, short priority);
    int (*add_url)(struct _LTASK *, char *host, char *path);
    int (*pop_url)(struct _LTASK *, char *url);
    int (*set_url_status)(struct _LTASK *, char *url, short status);
    int (*set_url_priority)(struct _LTASK *, char *url, short priority);
    int (*update_url_content)(struct _LTASK *, char *url, int date, short type, 
            char *content, int ncontent);
    void (*clean)(struct _LTASK **);
}LTASK;
/* initialize LTASK */
LTASK *ltask_init();
#endif
