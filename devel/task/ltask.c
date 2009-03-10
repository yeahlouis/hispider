#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "trie.h"
#include "timer.h"
#include "kvmap.h"
#include "ltask.h"
#include "md5.h"
#include "queue.h"
#include "fqueue.h"
#include "logger.h"
#define _EXIT_(format...)                                                               \
do                                                                                      \
{                                                                                       \
    fprintf(stderr, "%s::%d ", __FILE__, __LINE__);                                     \
    fprintf(stderr, format);                                                            \
    _exit(-1);                                                                          \
}while(0)
#define _MMAP_(io, st, type, incre_num)                                                 \
do                                                                                      \
{                                                                                       \
    if(io.fd > 0 && incre_num > 0)                                                      \
    {                                                                                   \
        if(io.map && io.size > 0)                                                       \
        {                                                                               \
            msync(io.map, io.size, MS_SYNC);                                            \
            munmap(io.map, io.size);                                                    \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            if(fstat(io.fd, &st) != 0)                                                  \
            {                                                                           \
                _EXIT_("fstat(%d) failed, %s\n", io.fd, strerror(errno));               \
            }                                                                           \
            io.size = st.st_size;                                                       \
        }                                                                               \
        if(io.size == 0 || io.map)                                                      \
        {                                                                               \
            io.size += ((off_t)sizeof(type) * (off_t)incre_num);                        \
            ftruncate(io.fd, io.size);                                                  \
            io.left += incre_num;                                                       \
        }                                                                               \
        io.total = io.size/(off_t)sizeof(type);                                         \
        if((io.map = mmap(NULL, io.size, PROT_READ|PROT_WRITE, MAP_SHARED,              \
                        io.fd, 0)) == (void *)-1)                                       \
        {                                                                               \
            _EXIT_("mmap %d size:%lld failed, %s\n", io.fd,                             \
                    (long long int)io.size, strerror(errno));                           \
        }                                                                               \
    }                                                                                   \
}while(0)
#define _MUNMAP_(mp, size)                                                              \
do                                                                                      \
{                                                                                       \
    if(mp && size > 0)                                                                  \
    {                                                                                   \
        msync(mp, size, MS_SYNC);                                                       \
        munmap(mp, size);                                                               \
        mp = NULL;                                                                      \
    }                                                                                   \
}while(0)

/* mkdir */
int ltask_mkdir(char *path, int mode)
{
    char *p = NULL, fullpath[L_PATH_MAX];
    int ret = 0, level = -1;
    struct stat st;

    if(path)
    {
        strcpy(fullpath, path);
        p = fullpath;
        while(*p != '\0')
        {
            if(*p == '/' )
            {
                level++;
                while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
                if(level > 0)
                {
                    *p = '\0';
                    memset(&st, 0, sizeof(struct stat));
                    ret = stat(fullpath, &st);
                    if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                    if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                    *p = '/';
                }
            }
            ++p;
        }
        return 0;
    }
    return -1;
}

/* set basedir*/
int ltask_set_basedir(LTASK *task, char *dir)
{
    char path[L_PATH_MAX], host[L_HOST_MAX], *p = NULL, *pp = NULL, *end = NULL;
    void *dp = NULL, *olddp = NULL;
    unsigned char *ip = NULL;
    LHOST *host_node = NULL;
    struct stat st = {0};
    LPROXY *proxy = NULL;
    int n = 0, i = 0;

    if(task && dir)
    {
        /* state */
        sprintf(path, "%s/%s", dir, L_LOG_NAME);
        if(ltask_mkdir(path, 0755) != 0)
        {
            _EXIT_("mkdir -p %s failed, %s\n", path, strerror(errno));
        }
        LOGGER_INIT(task->logger, path);
        sprintf(path, "%s/%s", dir, L_STATE_NAME);
        if((task->state_fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(task->state_fd, &st) == 0)
            {
                if(st.st_size == 0) ftruncate(task->state_fd, sizeof(LSTATE));
                if((task->state = (LSTATE *)mmap(NULL, sizeof(LSTATE),PROT_READ|PROT_WRITE, 
                                MAP_SHARED, task->state_fd, 0)) == (void *)-1)
                {
                    _EXIT_("mmap %s failed, %s\n", path, strerror(errno));
                }
            }
            else
            {
                _EXIT_("state %s failed, %s\n", path, strerror(errno));
            }
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* proxy/task */
        sprintf(path, "%s/%s", dir, L_PROXY_NAME);
        if((task->proxyio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->proxyio, st, LPROXY, PROXY_INCRE_NUM);
            if((proxy = (LPROXY *)task->proxyio.map) && task->proxyio.total > 0)
            {
                task->proxyio.left = 0;
                i = 0;
                do
                {
                    if(proxy->status == PROXY_STATUS_OK)
                    {
                        ip = (unsigned char *)&(proxy->ip);
                        n = sprintf(host, "%d.%d.%d.%d:%d", ip[0], ip[1], 
                                ip[2], ip[3], proxy->port);
                        dp = (void *)((long)(i + 1));
                        TRIETAB_ADD(task->table, host, n, dp);
                        QUEUE_PUSH(task->qproxy, int, &i);
                    }
                    else
                    {
                       task->proxyio.left++;
                    }
                    ++proxy;
                }while(i++ < task->proxyio.total);
            }
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_TASK_NAME);
        FQUEUE_INIT(task->qtask, path, LNODE);
        /* host/key/ip/url/domain/document */
        sprintf(path, "%s/%s", dir, L_KEY_NAME);
        if((task->key_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_DOMAIN_NAME);
        if((task->domain_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_URL_NAME);
        if((task->url_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_META_NAME);
        if((task->meta_fd = open(path, O_CREAT|O_RDWR, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_DOC_NAME);
        if((task->doc_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_HOST_NAME);
        if((task->hostio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->hostio, st, LHOST, HOST_INCRE_NUM);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_IP_NAME);
        if((task->ipio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->ipio, st, int, IP_INCRE_NUM);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* host/table */
        if(task->hostio.total > 0 && (host_node = (LHOST *)(task->hostio.map))
                && fstat(task->domain_fd, &st) == 0 && st.st_size > 0)
        {
            if((pp = (char *)mmap(NULL, st.st_size, PROT_READ, 
                            MAP_PRIVATE, task->domain_fd, 0)) != (void *)-1)
            {
                i = 0;
                do
                {
                    if(host_node->host_off >= 0 && host_node->host_len > 0)
                    {
                        p = pp + host_node->host_off;
                        dp = (void *)((long)(i+1));
                        TRIETAB_RADD(task->table, p, host_node->host_len, dp);
                    }
                    else 
                    {
                        task->hostio.end = (off_t)((char *)host_node - (char *)task->hostio.map);
                        break;
                    }
                    n = host_node->ip_off + host_node->ip_count * sizeof(int);
                    if(n > task->ipio.end) task->ipio.end = n;
                    ++host_node;
                }while(i++ < task->hostio.total);
                munmap(pp, st.st_size);
            }
            else
            {
                _EXIT_("mmap domain(%d) failed, %s\n", task->domain_fd, strerror(errno));
            }
        }
        /* urlmap */
         if(fstat(task->key_fd, &st) == 0 && st.st_size > 0)
         {
             if((pp = (char *)mmap(NULL, st.st_size, PROT_READ, 
                             MAP_PRIVATE, task->key_fd, 0)) != (void *)-1)
             {
                 p = pp;
                 end = p + st.st_size;
                 i = 0;
                 do
                 {
                     dp = (void *)((long)(++i));
                     KVMAP_ADD(task->urlmap, p, dp, olddp);
                     p += MD5_LEN;
                 }while(p < end);
                 munmap(pp, st.st_size);
             }
             else
             {
                 _EXIT_("mmap domain(%d) failed, %s\n", task->domain_fd, strerror(errno));
             }
         }
        /* document */
        return 0;
    }
    return -1;
}

/* add proxy */
int ltask_add_proxy(LTASK *task, char *host)
{
    char *p = NULL, *e = NULL, *pp = NULL, *ps = NULL, ip[L_HOST_MAX];
    int n = 0, i = 0, ret = -1;
    struct stat st = {0};
    LPROXY *proxy = NULL;
    void *dp = NULL;

    if(task && host)
    {
        MUTEX_LOCK(task->mutex);
        p = host;
        ps = ip;
        while(*p != '\0')
        {
            if(*p == ':') {e = ps; pp = ps+1;}
            *ps++ = *p++;
        }
        *ps = '\0';
        n = p - host;
        TRIETAB_GET(task->table, host, n, dp);
        if(e && dp == NULL)
        {
            if(task->proxyio.left == 0){_MMAP_(task->proxyio, st, LPROXY, PROXY_INCRE_NUM);}
            if(task->proxyio.left > 0 && (proxy = (LPROXY *)(task->proxyio.map)))
            {
                i = 0;
                do
                {
                    if(proxy->status != (short)PROXY_STATUS_OK)
                    {
                        dp = (void *)((long)(i+1));
                        TRIETAB_ADD(task->table, host, n, dp);
                        QUEUE_PUSH(task->qproxy, int, &i);
                        proxy->status = (short)PROXY_STATUS_OK;
                        *e = '\0';
                        proxy->ip = inet_addr(ip);
                        proxy->port = (unsigned short)atoi(pp);
                        /*
                        unsigned char *s = (unsigned char *)&(proxy->ip);
                        fprintf(stdout, "%d::%s %d:%d.%d.%d.%d:%d\n", 
                                __LINE__, host, proxy->ip, 
                        s[0], s[1], s[2], s[3], proxy->port);
                        */
                        task->proxyio.left--;
                        ret = 0;
                        break;
                    }
                    ++proxy;
                }while(i++ < task->proxyio.total);
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* get random proxy */
int ltask_get_proxy(LTASK *task, LPROXY *proxy)
{
    int rand = 0, id = 0, ret = -1;
    LPROXY *node = NULL;

    if(task && proxy)
    {
        MUTEX_LOCK(task->mutex);
        if(QTOTAL(task->qproxy) > 0)
        {
            do
            {
                if(QUEUE_POP(task->qproxy, int, &id) == 0)
                {
                    node = (LPROXY *)(task->proxyio.map + id * sizeof(LPROXY));
                    if(node->status == PROXY_STATUS_OK)
                    {
                        memcpy(proxy, node, sizeof(LPROXY));
                        break;
                    }
                    else 
                    {
                        node = NULL;
                    }
                }else break;
            }while(node == NULL);
            if(node) ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* delete proxy */
int ltask_set_proxy_status(LTASK *task, int id, char *host, short status)
{
    int ret = -1, n = 0, i = -1;
    LPROXY *proxy = NULL;
    void *dp = NULL;

    if(task && (id >= 0  || host))
    {
        MUTEX_LOCK(task->mutex);
        if(host)
        {
            n = strlen(host);
            TRIETAB_GET(task->table, host, n, dp);
            if(dp) i = (long)dp - 1;
        }
        else i = id;
        if(i >= 0 && i < task->proxyio.total)
        {
            proxy = (LPROXY *)(task->proxyio.map + i * sizeof(LPROXY));
            proxy->status = status;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* pop host for DNS resolving */
int ltask_pop_host(LTASK *task, char *host)
{
    LHOST *host_node = NULL;
    int host_id = -1;

    if(task && host && task->hostio.total > 0 && task->hostio.current >= 0
            && task->hostio.current < task->hostio.total
            && task->hostio.map && task->hostio.map != (void *)-1)
    {
        MUTEX_LOCK(task->mutex);
        host_node = (LHOST *)(task->hostio.map + task->hostio.current * sizeof(LHOST));
        do
        {
            if(host_node->host_len == 0) break;
            if(host_node->host_len > 0 && host_node->ip_count == 0)
            {
                if(pread(task->domain_fd, host, host_node->host_len, host_node->host_off) > 0)
                {
                    host_id = task->hostio.current++;
                    host[host_node->host_len] = '\0';
                }
                break;
            }
            ++host_node;
            task->hostio.current++;
        }while(task->hostio.current < task->hostio.total);
        MUTEX_UNLOCK(task->mutex);
    }
    return host_id;
}

/* set host ips */
int ltask_set_host_ip(LTASK *task, char *host, int *ips, int nips)
{
    LHOST *host_node = NULL;
    int i = 0, n = 0, ret = -1;
    struct stat st = {0};
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            if((task->ipio.end + nips * sizeof(int)) >= task->ipio.size)
            {
                _MMAP_(task->ipio, st, int, IP_INCRE_NUM);
            }
            if(task->ipio.map && task->ipio.map != (void *)-1)
            {
                memcpy(task->ipio.map + task->ipio.end, ips, nips * sizeof(int));
                host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST));
                host_node->ip_off = task->ipio.end;
                host_node->ip_count = (short)nips;
                task->ipio.end += (off_t)(nips * sizeof(int));
                ret = 0;
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* get host ip */
int ltask_get_host_ip(LTASK *task, char *host)
{
    LHOST *host_node = NULL;
    int i = 0, *ips = NULL, n = 0, ip = -1;
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1
                && (host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST)))
                && host_node->ip_count > 0 && task->ipio.size > host_node->ip_off
                && task->ipio.map && task->ipio.map != (void *)-1
                && (ips = (int *)(task->ipio.map + host_node->ip_off)))
        {
            i = 0;
            if(host_node->ip_count > 1)
                i = random()%(host_node->ip_count);
            ip = ips[i];
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ip;
}

/* list host for DNS resolving */
void ltask_list_host_ip(LTASK *task, FILE *fp)
{
    char host[L_HOST_MAX];
    LHOST *host_node = NULL;
    int *ips = NULL, i = 0, x = 0;
    unsigned char *pp = NULL;

    if(task && task->hostio.total > 0)
    {
        MUTEX_LOCK(task->mutex);
        i = 0;
        host_node = (LHOST *)(task->hostio.map);
        do
        {
            if(host_node->host_len == 0) break;
            if(host_node->host_len > 0 && host_node->ip_count > 0 
                    && host_node->host_len < L_HOST_MAX 
                    && host_node->ip_off < task->ipio.size
                    && task->ipio.map && task->ipio.map != (void *)-1)
            {
                if(pread(task->domain_fd, host, host_node->host_len, host_node->host_off) > 0)
                {
                    host[host_node->host_len] = '\0';
                    ips = (int *)(task->ipio.map + host_node->ip_off);
                    fprintf(stdout, "[%d][%s]", i, host);
                    x = host_node->ip_count;
                    while(--x >= 0)
                    {
                        pp = (unsigned char *)&(ips[x]);
                        fprintf(stdout, "[%d.%d.%d.%d]", pp[0], pp[1], pp[2], pp[3]);
                    }
                    fprintf(stdout, "\n");
                }
            }
            ++host_node;
        }while(i++ < task->hostio.total);
        MUTEX_UNLOCK(task->mutex);
    }
    return ;
}

/* set host status */
int ltask_set_host_status(LTASK *task, char *host, int status)
{
    LHOST *host_node = NULL;
    int i = 0, n = 0, ret = -1;
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST));
            host_node->status = (short)status;
            ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* set host priority */
int ltask_set_host_priority(LTASK *task, char *host, int priority)
{
    LHOST *host_node = NULL;
    int i = 0, n = 0, ret = -1;
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST));
            ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* add url */
int ltask_add_url(LTASK *task, char *url)
{
    char newurl[L_URL_MAX], *p = NULL, *pp = NULL, *e = NULL, *host = NULL;
    void *dp = NULL, *olddp = NULL;
    int ret = -1, n = 0, nurl = 0, id = 0, host_id = 0;
    LHOST *host_node = NULL;
    unsigned char key[MD5_LEN];
    struct stat st = {0};
    LMETA meta = {0};

    if(task && url)
    {
        MUTEX_LOCK(task->mutex);
        p = url;
        pp = newurl;
        while(*p != '\0')
        {
            if(host == NULL && *p == ':' && *(p+1) == '/' && *(p+2) == '/')
            {
                *pp++ = ':';
                *pp++ = '/';
                *pp++ = '/';
                p += 3;
                host = pp;
                continue;
            }
            if(host && e == NULL && *p == '/') e = pp;
            if(*p >= 'A' && *p <= 'Z')
            {
                *pp++ = *p++ + ('a' - 'A');
            }
            else
            {
                *pp++ = *p++;
            }
        }
        if(host == NULL || e == NULL) goto err;
        *pp = '\0';
        /* check/add host */
        if((nurl = (p - url)) <= 0 || (n = (e - host)) <= 0) goto err;
        TRIETAB_RGET(task->table, host, n, dp);
        if(dp == NULL)
        {
            if(task->hostio.end >= task->hostio.size)
            {_MMAP_(task->hostio, st, LHOST, HOST_INCRE_NUM);}
            host_id = task->hostio.end/(off_t)sizeof(LHOST);
            host_node = (LHOST *)(task->hostio.map + task->hostio.end);
	        DEBUG_LOGGER(task->logger, "%d:url[%d:%s] URL[%d:%s] path[%s]", 
                    host_id, n, newurl, nurl, url, e);
            if(fstat(task->domain_fd, &st) != 0) goto err;
            host_node->host_off = st.st_size;
            host_node->host_len = n;
            *e = '\n';
            if(pwrite(task->domain_fd, host, n+1, st.st_size) <= 0) goto err;
            *e = '/';
            dp = (void *)((long)(host_id + 1));
            TRIETAB_RADD(task->table, host, n, dp);
            task->hostio.end += (off_t)sizeof(LHOST);
            task->state->host_total++;
        }
        else
        {
            host_id = (long)dp - 1;
            host_node = (LHOST *)(task->hostio.map + (host_id * sizeof(LHOST)));
	        DEBUG_LOGGER(task->logger, "%d:url[%d:%s] URL[%d:%s] path[%s] left:%d total:%d", 
                    host_id, n, newurl, nurl, url, e, host_node->url_left, host_node->url_total);
        }
        /* check/add url */
        if((n = pp - newurl) <= 0) goto err;
        memset(key, 0, MD5_LEN);
        md5((unsigned char *)newurl, n, key);
        if(fstat(task->meta_fd, &st) != 0) goto err;
        id = (st.st_size/(off_t)sizeof(LMETA));
        dp = (void *)((long)id);
        KVMAP_ADD(task->urlmap, key, dp, olddp); 
        if(olddp == NULL)
        {
            if(fstat(task->url_fd, &st) != 0) goto err;
            if((n = sprintf(newurl, "%s\n", url)) > 0 
                    &&  write(task->url_fd, newurl, n) > 0 
                    && write(task->key_fd, key, MD5_LEN) > 0)
            {
                meta.url_off = st.st_size;
                meta.url_len = n - 1;
                meta.host_id = host_id;
                meta.prev    = host_node->url_last_id;
                meta.next    = -1;
                if(host_node->url_total == 0) 
                {
                    meta.prev = -1;
                    host_node->url_current_id = host_node->url_first_id = id;
                    //add to queue
                }
                else
                {
                    pwrite(task->meta_fd, &id, sizeof(int), 
                            (off_t)(host_node->url_last_id+1) 
                            * (off_t)sizeof(LMETA) - sizeof(int));
                }
                host_node->url_left++;
                host_node->url_last_id = id;
                host_node->url_total++;
                pwrite(task->meta_fd, &meta, sizeof(LMETA), (off_t)id * (off_t)sizeof(LMETA));
                ret = 0;
            }
        }
err: 
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* pop url */
int ltask_pop_url(LTASK *task, char *url)
{
    int urlid = -1, n = -1, x = 0;
    LHOST *host_node = NULL;
    LNODE node = {0};
    LMETA meta = {0};

    if(task && url)
    {
        MUTEX_LOCK(task->mutex);
        if(FQUEUE_POP(task->qtask, LNODE, &node) == 0)
        {
            if(node.type == Q_TYPE_HOST && node.id >= 0)
            {
                host_node = (LHOST *)(task->hostio.map + node.id * sizeof(LHOST));
                urlid = host_node->url_current_id;
            }
            else if(node.type == Q_TYPE_URL && node.id >= 0)
            {
                urlid = node.id;
            }
            else goto end;
        }
        else
        {
            x = task->state->host_current;
            do
            {
                host_node = (LHOST *)(task->hostio.map 
                        + task->state->host_current * sizeof(LHOST));
                if(task->state->host_current++ == task->state->host_total) 
                    task->state->host_current = 0;
                if(host_node && host_node->status >= 0 && host_node->url_left > 0)
                {
                    urlid = host_node->url_current_id;
                    DEBUG_LOGGER(task->logger, "urlid:%d current:%d left:%d total:%d", urlid, 
                        task->state->host_current, host_node->url_left, host_node->url_total);
                    break;
                }
                else host_node = NULL;
                if(x == task->state->host_current) break;
            }while(host_node == NULL);
        }
        /* read url */
        if(urlid >= 0 && pread(task->meta_fd, &meta, sizeof(LMETA), 
                    (off_t)(urlid * sizeof(LMETA))) > 0 && meta.url_len <= L_URL_MAX
                && (n = pread(task->url_fd, url, meta.url_len, meta.url_off)) > 0)
        {
            if(host_node == NULL) 
                host_node = (LHOST *)(task->hostio.map + meta.host_id * sizeof(LHOST));
            url[n] = '\0';
            host_node->url_current_id = meta.next;
            host_node->url_left--;
        }
end:
        MUTEX_UNLOCK(task->mutex);
    }
    return urlid;
}

/* set url status */
int ltask_set_url_status(LTASK *task, char *url, int status)
{

}

/* set url priority */
int ltask_set_url_priority(LTASK *task, char *url, int priority)
{
}

/* update url content  */
int ltask_update_url_content(LTASK *task, int urlid, char *content, int ncontent)
{
    
}

/* clean */
void ltask_clean(LTASK **ptask)
{
    if(ptask && *ptask)
    {
        if((*ptask)->mutex) {MUTEX_DESTROY((*ptask)->mutex);}
        if((*ptask)->logger) {TIMER_CLEAN((*ptask)->logger);}
        if((*ptask)->timer) {TIMER_CLEAN((*ptask)->timer);}
        if((*ptask)->urlmap) {KVMAP_CLEAN((*ptask)->urlmap);}
        if((*ptask)->table) {TRIETAB_CLEAN((*ptask)->table);}
        if((*ptask)->qtask){FQUEUE_CLEAN((*ptask)->qtask);}
        if((*ptask)->qproxy){QUEUE_CLEAN((*ptask)->qproxy);}
        if((*ptask)->key_fd > 0) close((*ptask)->key_fd);
        if((*ptask)->url_fd > 0) close((*ptask)->url_fd);
        if((*ptask)->domain_fd > 0) close((*ptask)->domain_fd);
        if((*ptask)->doc_fd > 0) close((*ptask)->doc_fd);
        if((*ptask)->state_fd > 0) 
        {
            _MUNMAP_((*ptask)->state, sizeof(LSTATE));
            close((*ptask)->state_fd);
        }
        if((*ptask)->proxyio.fd > 0)
        {
            _MUNMAP_((*ptask)->proxyio.map, (*ptask)->proxyio.size);
            close((*ptask)->proxyio.fd);
        }
        if((*ptask)->hostio.fd > 0)
        {
            _MUNMAP_((*ptask)->hostio.map, (*ptask)->hostio.size);
            close((*ptask)->hostio.fd);
        }
        if((*ptask)->ipio.fd > 0)
        {
            _MUNMAP_((*ptask)->ipio.map, (*ptask)->ipio.size);
            close((*ptask)->ipio.fd);
        }
	free(*ptask);
	*ptask = NULL;
    }
}

/* initialize */
LTASK *ltask_init()
{
    LTASK *task = NULL;

    if((task = (LTASK *)calloc(1, sizeof(LTASK))))
    {
        KVMAP_INIT(task->urlmap);
        TRIETAB_INIT(task->table);
        TIMER_INIT(task->timer);
        MUTEX_INIT(task->mutex);
        QUEUE_INIT(task->qproxy);
        task->set_basedir           = ltask_set_basedir;
        task->add_proxy             = ltask_add_proxy;
        task->get_proxy             = ltask_get_proxy;
        task->set_proxy_status      = ltask_set_proxy_status;
        task->pop_host              = ltask_pop_host;
        task->set_host_ip           = ltask_set_host_ip;
        task->get_host_ip           = ltask_get_host_ip;
        task->list_host_ip          = ltask_list_host_ip;
        task->set_host_status       = ltask_set_host_status;
        task->add_url               = ltask_add_url;
        task->pop_url               = ltask_pop_url;
        task->clean                 = ltask_clean;
    }
    return task;
}


#ifdef _DEBUG_LTASK
static char *proxylist[] = 
{
    "66.104.77.20:3128",
    "164.73.47.244:3128",
    "200.228.43.202:3128",
    "121.22.29.180:80",
    "202.181.184.203:80",
    "86.0.64.246:9090",
    "204.8.155.226:3124",
    "129.24.211.25:3128",
    "217.91.6.207:8080",
    "202.27.17.175:80",
    "128.233.252.12:3124",
    "67.69.254.244:80",
    "192.33.90.66:3128",
    "203.160.1.94:80",
    "201.229.208.2:80",
    "130.37.198.244:3127",
    "155.246.12.163:3124",
    "141.24.33.192:3128",
    "193.188.112.21:80",
    "128.223.8.111:3127",
    "67.69.254.243:80",
    "212.93.193.72:443",
    "141.24.33.192:3124",
    "121.22.29.182:80",
    "221.203.154.26:8080",
    "203.160.1.112:80",
    "193.39.157.48:80",
    "130.37.198.244:3128",
    "129.24.211.25:3124",
    "195.116.60.34:3127",
    "199.239.136.200:80",
    "199.26.254.65:3128",
    "193.39.157.15:80",
    "218.28.58.86:3128",
    "60.12.227.209:3128",
    "128.233.252.12:3128",
    "137.226.138.154:3128",
    "67.69.254.240:80",
    "152.3.138.5:3128",
    "142.150.238.13:3124",
    "199.239.136.245:80",
    "203.160.1.66:80",
    "123.130.112.17:8080",
    "203.160.1.103:80",
    "198.82.160.220:3124"
};
#define NPROXY 45
static char *urllist[] = 
{
	"http://news.sina.com.cn/", 
	"http://mil.news.sina.com.cn/", 
	"http://news.sina.com.cn/society/", 
	"http://blog.sina.com.cn/", 
	"http://blog.sina.com.cn/lm/ruiblog/index.html?tj=1", 
	"http://blog.sina.com.cn/lm/rank/index.html?tj=1", 
	"http://news.sina.com.cn/guide/", 
	"http://weather.news.sina.com.cn/", 
	"http://news.sina.com.cn/health/index.shtml", 
	"http://news.sina.com.cn/china/", 
	"http://news.sina.com.cn/world/", 
	"http://sky.news.sina.com.cn/", 
	"http://news.sina.com.cn/opinion/index.shtml", 
	"http://news.sina.com.cn/interview/index.shtml", 
	"http://news.sina.com.cn/photo/", 
	"http://survey.news.sina.com.cn/list.php?channel=news&dpc=1", 
	"http://news.sina.com.cn/news1000/", 
	"http://news.sina.com.cn/hotnews/", 
	"http://news.sina.com.cn/zt/", 
	"http://news.sina.com.cn/w/p/2009-01-28/131217118076.shtml", 
	"http://news.sina.com.cn/z/2009europedavostrip/index.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/164817118372.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/110515088916s.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/090617117716.shtml", 
	"http://news.sina.com.cn/z/2009chunjie/index.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/061517117393.shtml", 
	"http://news.sina.com.cn/z/video/2009chunjie/index.shtml", 
	"http://blog.sina.com.cn/lm/z/2009chunjie/index.html", 
	"http://news.sina.com.cn/z/2009chunyun/index.shtml", 
	"http://comment4.news.sina.com.cn/comment/skin/simple.html?channel=yl&newsid=28-19-3738&style=1", 
	"http://news.sina.com.cn/c/2009-01-28/140817118125.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/171417118397.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/032417117117.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/103015088902s.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/050617117332.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/101215088878s.shtml" 
};
#define NURL 36
int main()
{
    LTASK *task = NULL;
    LPROXY proxy = {0};
    char *basedir = "/tmp/html", *p = NULL, 
         host[L_HOST_MAX], url[L_URL_MAX], ip[L_IP_MAX];
    unsigned char *pp = NULL;
    int i = 0, n = 0, urlid = -1;

    if((task = ltask_init()))
    {
        task->set_basedir(task, basedir);
        /* proxy */
        fprintf(stdout, "%d::qtotal:%d\n", __LINE__, QTOTAL(task->qproxy));
        for(i = 0; i < NPROXY; i++)
        {
            p = proxylist[i];
            task->add_proxy(task, p);
        }
        fprintf(stdout, "%d::qtotal:%d\n", __LINE__, QTOTAL(task->qproxy));
        task->set_proxy_status(task, -1, "198.82.160.220:3124", PROXY_STATUS_OK);
        task->set_proxy_status(task, -1, "199.239.136.245:80", PROXY_STATUS_OK);
        task->set_proxy_status(task, -1, "142.150.238.13:3124", PROXY_STATUS_OK);
        i = 0;
        while(task->get_proxy(task, &proxy) == 0)
        {
            pp = (unsigned char *)&(proxy.ip);
            fprintf(stdout, "%d::[%d][%d.%d.%d.%d:%d]\n", __LINE__, 
                    i++, pp[0], pp[1], pp[2], pp[3], proxy.port);
        }
        /* host/url */
        for(i = 0; i < NURL; i++)
        {
            p = urllist[i];
            task->add_url(task, p);
        }
        i = 0;
        while(task->pop_host(task, host) >= 0)
        {
            sprintf(ip, "202.0.16.%d", (random()%256));
            n = inet_addr(ip);
            pp = (unsigned char *)&n;
            fprintf(stdout, "[%d.%d.%d.%d]\n", pp[0], pp[1], pp[2], pp[3]);
            task->set_host_ip(task, host, &n, 1);
            task->set_host_status(task, host, HOST_STATUS_OK);
            n = task->get_host_ip(task, host);
            pp = (unsigned char *)&n;
            fprintf(stdout, "%d::[%d][%s][%d.%d.%d.%d]\n",
                    __LINE__, i++, host, pp[0], pp[1], pp[2], pp[3]);
        }
        task->list_host_ip(task, stdout);
        while((urlid = task->pop_url(task, url)) >= 0)
        {
            fprintf(stdout, "%d::url[%s]\n", urlid, url);
        }
        task->clean(&task);
    }
}
//gcc -o task ltask.c utils/*.c -I utils/ -D_DEBUG_LTASK && ./task
#endif