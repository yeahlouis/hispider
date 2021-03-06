#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "mmtree.h"
#include "mutex.h"
#define MMT(px) ((MMTREE *)px)
#define MMT_COLOR_BLACK  0
#define MMT_COLOR_RED    1
#define MMT_MUNMAP(x)                                                           \
do                                                                              \
{                                                                               \
    if(x && MMT(x)->size > 0)                                                   \
    {                                                                           \
        if(MMT(x)->start && MMT(x)->start != (void *)-1)                        \
        {                                                                       \
            msync(MMT(x)->start, MMT(x)->size, MS_SYNC);                        \
            munmap(MMT(x)->start, MMT(x)->size);                                \
            MMT(x)->start = NULL;                                               \
            MMT(x)->state = NULL;                                               \
            MMT(x)->map = NULL;                                                 \
        }                                                                       \
    }                                                                           \
}while(0)

#define MMT_MMAP(x)                                                             \
do                                                                              \
{                                                                               \
    if(x)                                                                       \
    {                                                                           \
        MMT_MUNMAP(x);                                                          \
        if((MMT(x)->start = mmap(NULL, MMT(x)->size, PROT_READ|PROT_WRITE,      \
                        MAP_SHARED, MMT(x)->fd, 0)) != (void *)-1)              \
        {                                                                       \
            MMT(x)->state = (MTSTATE *)MMT(x)->start;                           \
            MMT(x)->map = (MTNODE *)(MMT(x)->start + sizeof(MTSTATE));          \
        }                                                                       \
    }                                                                           \
}while(0)

#define MMT_INCRE(x)                                                            \
do                                                                              \
{                                                                               \
    if(x)                                                                       \
    {                                                                           \
        if(MMT(x)->start && MMT(x)->size > 0)                                   \
        {                                                                       \
            msync(MMT(x)->start, MMT(x)->size, MS_SYNC);                        \
            munmap(MMT(x)->start, MMT(x)->size);                                \
            MMT(x)->start = NULL;                                               \
            MMT(x)->state = NULL;                                               \
            MMT(x)->map = NULL;                                                 \
        }                                                                       \
        MMT(x)->size += (off_t)MMTREE_INCRE_NUM * (off_t)sizeof(MTNODE);        \
        ftruncate(MMT(x)->fd, MMT(x)->size);                                    \
        if((MMT(x)->start = mmap(NULL, MMT(x)->size, PROT_READ|PROT_WRITE,      \
                        MAP_SHARED, MMT(x)->fd, 0)) != (void *)-1)              \
        {                                                                       \
            MMT(x)->state = (MTSTATE *)(MMT(x)->start);                         \
            MMT(x)->map = (MTNODE *)((char*)(MMT(x)->start)+sizeof(MTSTATE));   \
            MMT(x)->state->left += MMTREE_INCRE_NUM;                            \
            if(MMT(x)->state->total == 0) MMT(x)->state->left--;                \
            MMT(x)->state->total += MMTREE_INCRE_NUM;                           \
        }                                                                       \
    }                                                                           \
}while(0)

#define MMT_ROTATE_LEFT(x, prootid, id, lid, rid, ppid)                         \
do                                                                              \
{                                                                               \
    if(x && (rid = MMT(x)->map[id].right) > 0)                                  \
    {                                                                           \
        if((lid = MMT(x)->map[id].right = MMT(x)->map[rid].left) > 0)           \
        {                                                                       \
            MMT(x)->map[lid].parent = id;                                       \
        }                                                                       \
        if((ppid = MMT(x)->map[rid].parent = MMT(x)->map[id].parent) > 0)       \
        {                                                                       \
            if(MMT(x)->map[ppid].left == id)                                    \
            MMT(x)->map[ppid].left = rid;                                       \
            else                                                                \
            MMT(x)->map[ppid].right = rid;                                      \
        }else *prootid = rid;                                                   \
        MMT(x)->map[rid].left = id;                                             \
        MMT(x)->map[id].parent = rid;                                           \
    }                                                                           \
}while(0)

#define MMT_ROTATE_RIGHT(x, prootid, id, lid, rid, ppid)                        \
do                                                                              \
{                                                                               \
    if(x && (lid = MMT(x)->map[id].left) > 0)                                   \
    {                                                                           \
        if((rid = MMT(x)->map[id].left = MMT(x)->map[lid].right) > 0)           \
            MMT(x)->map[rid].parent = id;                                       \
        if((ppid = MMT(x)->map[lid].parent = MMT(x)->map[id].parent) > 0)       \
        {                                                                       \
            if(MMT(x)->map[ppid].left == id)                                    \
                MMT(x)->map[ppid].left = lid;                                   \
            else                                                                \
                MMT(x)->map[ppid].right = lid;                                  \
        }                                                                       \
        else *prootid = lid;                                                    \
        MMT(x)->map[lid].right = id;                                            \
        MMT(x)->map[id].parent = lid;                                           \
    }                                                                           \
}while(0)

#define MMT_INSERT_COLOR(x, prootid, id, lid, rid, uid, pid, gpid, ppid)        \
do                                                                              \
{                                                                               \
    while((pid = MMT(x)->map[id].parent)> 0                                     \
            && MMT(x)->map[pid].color == MMT_COLOR_RED)                         \
    {                                                                           \
        gpid = MMT(x)->map[pid].parent;                                         \
        if(pid == MMT(x)->map[gpid].left)                                       \
        {                                                                       \
            uid = MMT(x)->map[gpid].right;                                      \
            if(uid > 0 && MMT(x)->map[uid].color == MMT_COLOR_RED)              \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[pid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[gpid].color = MMT_COLOR_RED;                        \
                id = gpid;                                                      \
                continue;                                                       \
            }                                                                   \
            if(MMT(x)->map[pid].right == id)                                    \
            {                                                                   \
                MMT_ROTATE_LEFT(x, prootid, pid, rid, lid, ppid);               \
                uid = pid; pid = id; id = uid;                                  \
            }                                                                   \
            MMT(x)->map[pid].color = MMT_COLOR_BLACK;                           \
            MMT(x)->map[gpid].color = MMT_COLOR_RED;                            \
            MMT_ROTATE_RIGHT(x, prootid, gpid, lid, rid, ppid);                 \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            uid = MMT(x)->map[gpid].left;                                       \
            if(uid > 0 && MMT(x)->map[uid].color == MMT_COLOR_RED)              \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[pid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[gpid].color = MMT_COLOR_RED;                        \
                id = gpid;                                                      \
                continue;                                                       \
            }                                                                   \
            if(MMT(x)->map[pid].left == id)                                     \
            {                                                                   \
                MMT_ROTATE_RIGHT(x, prootid, pid, lid, rid, ppid);              \
                uid = pid; pid = id; id = uid;                                  \
            }                                                                   \
            MMT(x)->map[pid].color = MMT_COLOR_BLACK;                           \
            MMT(x)->map[gpid].color = MMT_COLOR_RED;                            \
            MMT_ROTATE_LEFT(x, prootid, gpid, rid, lid, ppid);                  \
        }                                                                       \
    }                                                                           \
    if(*prootid > 0)MMT(x)->map[*prootid].color = MMT_COLOR_BLACK;              \
}while(0)

#define MMT_REMOVE_COLOR(x, prootid, id, pid, lid, rid, uid, ppid)              \
do                                                                              \
{                                                                               \
    while((id == 0 || MMT(x)->map[id].color == MMT_COLOR_BLACK)                 \
            && id != *prootid)                                                  \
    {                                                                           \
        if(MMT(x)->map[pid].left == id)                                         \
        {                                                                       \
            uid = MMT(x)->map[pid].right;                                       \
            if(MMT(x)->map[uid].color == MMT_COLOR_RED)                         \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[pid].color = MMT_COLOR_RED;                         \
                MMT_ROTATE_LEFT(x, prootid, pid, rid, lid, ppid);               \
                uid = MMT(x)->map[pid].right;                                   \
            }                                                                   \
            if((MMT(x)->map[uid].left == 0                                      \
                        || MMT(x)->map[uid].color == MMT_COLOR_BLACK)               \
                    && (MMT(x)->map[uid].right == 0                                 \
                        || MMT(x)->map[uid].color == MMT_COLOR_BLACK))              \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_RED;                         \
                id = pid;                                                       \
                pid = MMT(x)->map[id].parent;                                   \
            }                                                                   \
            else                                                                \
            {                                                                   \
                rid = MMT(x)->map[uid].right;                                   \
                lid = MMT(x)->map[uid].left;                                    \
                if(rid == 0 || MMT(x)->map[rid].color == MMT_COLOR_BLACK)       \
                {                                                               \
                    MMT(x)->map[lid].color = MMT_COLOR_BLACK;                   \
                    MMT(x)->map[uid].color = MMT_COLOR_RED;                     \
                    MMT_ROTATE_RIGHT(x, prootid, uid, lid, rid, ppid);          \
                    uid = MMT(x)->map[pid].right;                               \
                }                                                               \
                MMT(x)->map[uid].color = MMT(x)->map[pid].color;                \
                MMT(x)->map[pid].color = MMT_COLOR_BLACK;                       \
                if((rid = MMT(x)->map[uid].right) > 0)                          \
                MMT(x)->map[rid].color = MMT_COLOR_BLACK;                       \
                MMT_ROTATE_LEFT(x, prootid, pid, rid, lid, ppid);               \
                id = *prootid;                                                  \
                break;                                                          \
            }                                                                   \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            uid = MMT(x)->map[pid].left;                                        \
            if(MMT(x)->map[uid].color == MMT_COLOR_RED)                         \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_BLACK;                       \
                MMT(x)->map[pid].color = MMT_COLOR_RED;                         \
                MMT_ROTATE_RIGHT(x, prootid, pid, lid, rid, ppid);              \
                uid = MMT(x)->map[pid].left;                                    \
            }                                                                   \
            if((MMT(x)->map[uid].left == 0                                      \
                        || MMT(x)->map[uid].color == MMT_COLOR_BLACK)           \
                    && (MMT(x)->map[uid].right == 0                             \
                        || MMT(x)->map[uid].color == MMT_COLOR_BLACK))          \
            {                                                                   \
                MMT(x)->map[uid].color = MMT_COLOR_RED;                         \
                id = pid;                                                       \
                pid = MMT(x)->map[id].parent;                                   \
            }                                                                   \
            else                                                                \
            {                                                                   \
                rid = MMT(x)->map[uid].right;                                   \
                lid = MMT(x)->map[uid].left;                                    \
                if(lid == 0 || MMT(x)->map[lid].color == MMT_COLOR_BLACK)       \
                {                                                               \
                    MMT(x)->map[rid].color = MMT_COLOR_BLACK;                   \
                    MMT(x)->map[uid].color = MMT_COLOR_RED;                     \
                    MMT_ROTATE_LEFT(x, prootid, uid, rid, lid, ppid);           \
                    uid = MMT(x)->map[pid].left;                                \
                }                                                               \
                MMT(x)->map[uid].color = MMT(x)->map[pid].color;                \
                MMT(x)->map[pid].color = MMT_COLOR_BLACK;                       \
                if((lid = MMT(x)->map[uid].left) > 0)                           \
                MMT(x)->map[lid].color = MMT_COLOR_BLACK;                       \
                MMT_ROTATE_RIGHT(x, prootid, pid, lid, rid, ppid);              \
                id = *prootid;                                                  \
                break;                                                          \
            }                                                                   \
        }                                                                       \
    }                                                                           \
    if(id > 0) MMT(x)->map[id].color = MMT_COLOR_BLACK;                         \
}while(0)
/* init mmtree */
void *mmtree_init(char *file)
{
    void *x = NULL;
    struct stat  st = {0};

    if((x = (MMTREE *)calloc(1, sizeof(MMTREE))))
    {
        if((MMT(x)->fd = open(file, O_CREAT|O_RDWR, 0644)) > 0 
                && fstat(MMT(x)->fd, &st) == 0)
        {
            MUTEX_INIT(MMT(x)->mutex);
            MMT(x)->size = st.st_size;
            //init truncate
            if(st.st_size == 0)
            {
                MMT(x)->size += (off_t)sizeof(MTSTATE);
                MMT_INCRE(x);
            }
            else
            {
                //mmap
                MMT_MMAP(x);
            }
        }
        else 
        {
            if(MMT(x)->fd > 0) close(MMT(x)->fd);
            free(x);
            x = NULL;
        }
    }
    return x;
}

/* insert new root */
int mmtree_new_tree(void *x, int key, int data)
{
    int id = -1;
    if(x)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->state && MMT(x)->map)
        {
            if(MMT(x)->state->left < 2)
            {
                MMT_INCRE(x);
            }
            if(MMT(x)->state->qleft > 0)
            {
                id = MMT(x)->state->qfirst;
                MMT(x)->state->qfirst = MMT(x)->map[id].parent;
                MMT(x)->state->qleft--;
            }
            else
            {
                id = ++(MMT(x)->state->current);
            }
            memset(&(MMT(x)->map[id]), 0, sizeof(MTNODE));
            if(MMT(x)->map && MMT(x)->state)
            {
                //fprintf(stdout, "%s::%d id:%d current:%d total:%d\n", __FILE__, __LINE__, id, MMT(x)->state->current, MMT(x)->state->total);
                MMT(x)->map[id].key = key;
                MMT(x)->map[id].data = data;
                MMT(x)->map[id].color = MMT_COLOR_BLACK;
                MMT(x)->state->left--;
            }
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* insert new node */
int mmtree_insert(void *x, int *prootid, int key, int data, int *old)
{
    int id = 0, nodeid = 0, rid = 0, lid = 0, uid = 0, pid = 0, gpid = 0, ppid = 0;
    MTNODE *node = NULL;

    if(x && prootid  && *prootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->state && MMT(x)->map && *prootid < MMT(x)->state->total)
        {
            nodeid = *prootid;
            while(nodeid > 0 && nodeid < MMT(x)->state->total)
            {
                node = &(MMT(x)->map[nodeid]);
                if(key == node->key)
                {
                    id = nodeid;
                    if(old) *old = node->data;
                    goto end;
                }
                else if(key > node->key)
                {
                    if(node->right == 0) break;
                    nodeid = node->right;
                }
                else 
                {
                    if(node->left == 0) break;
                    nodeid = node->left;
                }
            }
            //new node
            if(id == 0)
            {
                //fprintf(stdout, "%s::%d %d start:%p state:%p map:%p current:%d left:%d total:%d qleft:%d qfirst:%d qlast:%d\n", __FILE__, __LINE__, id, MMT(x)->start, MMT(x)->state, MMT(x)->map, MMT(x)->state->current, MMT(x)->state->left, MMT(x)->state->total, MMT(x)->state->qleft, MMT(x)->state->qfirst, MMT(x)->state->qlast);
                if(MMT(x)->state->left == 0)
                {
                    MMT_INCRE(x);
                }
                if(MMT(x)->state->qleft > 0)
                {
                    id = MMT(x)->state->qfirst;
                    MMT(x)->state->qfirst = MMT(x)->map[id].parent;
                    MMT(x)->state->qleft--;
                }
                else
                {
                    id = ++(MMT(x)->state->current);
                }
                //fprintf(stdout, "%s::%d %d start:%p state:%p map:%p current:%d left:%d total:%d qleft:%d qfirst:%d qlast:%d\n", __FILE__, __LINE__, id, MMT(x)->start, MMT(x)->state, MMT(x)->map, MMT(x)->state->current, MMT(x)->state->left, MMT(x)->state->total, MMT(x)->state->qleft, MMT(x)->state->qfirst, MMT(x)->state->qlast);
                MMT(x)->state->left--;
                //memset(&(MMT(x)->map[id]), 0, sizeof(MTNODE));
                MMT(x)->map[id].parent = nodeid;
                MMT(x)->map[id].key = key;
                MMT(x)->map[id].data = data;
                if(key > MMT(x)->map[nodeid].key) 
                    MMT(x)->map[nodeid].right = id;
                else
                    MMT(x)->map[nodeid].left = id;
            }
            else
            {
                //fprintf(stdout, "%s::%d old id:%d pid:%d key:%d\n", __FILE__, __LINE__, id, parentid, key);
            }
        }
        if((nodeid = id) > 0)
        {
            MMT(x)->map[nodeid].color = MMT_COLOR_RED;
            MMT_INSERT_COLOR(x, prootid, nodeid, lid, rid, uid, pid, gpid, ppid);
        }
end:
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* get node key/data */
int mmtree_get(void *x, int tnodeid, int *key, int *data)
{
    int id = -1;

    if(x && tnodeid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && tnodeid <  MMT(x)->state->total)
        {
            *key = MMT(x)->map[tnodeid].key;
            *data = MMT(x)->map[tnodeid].data;
            id = tnodeid;
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* find key/data */
int mmtree_find(void *x, int rootid, int key, int *data)
{
    int id = -1;

    if(x && rootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && rootid <  MMT(x)->state->total)
        {
            id = rootid;
            while(id > 0 && id < MMT(x)->state->total)
            {
                if(key == MMT(x)->map[id].key)
                {
                    if(data) *data = MMT(x)->map[id].data;
                    break;
                }
                else if(key > MMT(x)->map[id].key)
                {
                    id = MMT(x)->map[id].right;
                }
                else
                {
                    id = MMT(x)->map[id].left;
                }
            }
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}
/* get tree->min key/data */
int mmtree_min(void *x, int rootid, int *key, int *data)
{
    int id = -1;

    if(x && rootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && rootid <  MMT(x)->state->total)
        {
            id = rootid;
            while(MMT(x)->map[id].left > 0)
            {
                id = MMT(x)->map[id].left;
            }
            if(id > 0 && MMT(x)->state->total)
            {
                if(key) *key = MMT(x)->map[id].key;
                if(data) *data = MMT(x)->map[id].data;
            }
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* get tree->max key/data */
int mmtree_max(void *x, int rootid, int *key, int *data)
{
    int id = -1;

    if(x && rootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && rootid <  MMT(x)->state->total)
        {
            id = rootid;
            while(MMT(x)->map[id].right > 0)
            {
                id = MMT(x)->map[id].right;
            }
            if(id > 0 && MMT(x)->state->total)
            {
                if(key) *key = MMT(x)->map[id].key;
                if(data) *data = MMT(x)->map[id].data;
            }
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* get next node key/data */
int mmtree_next(void *x, int rootid, int tnodeid, int *key, int *data)
{
    int id = -1, parentid = 0;

    if(x && tnodeid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && tnodeid <  MMT(x)->state->total)
        {
            id = tnodeid;
            if(MMT(x)->map[id].right > 0)
            {
                id = MMT(x)->map[id].right;
                while(MMT(x)->map[id].left  > 0)
                {
                    id = MMT(x)->map[id].left;
                }
                //fprintf(stdout, "%s::%d id:%d\n", __FILE__, __LINE__, id);
            }
            else
            {
                while(id > 0)
                {
                    parentid = MMT(x)->map[id].parent;
                    if(MMT(x)->map[id].key < MMT(x)->map[parentid].key)
                    {
                        id = parentid;
                        goto end;
                    }
                    else
                    {
                        id = parentid;
                    }
                }
                //fprintf(stdout, "%s::%d id:%d\n", __FILE__, __LINE__, id);
            }
end:
            if(id > 0 && id < MMT(x)->state->total)
            {
                if(key) *key = MMT(x)->map[id].key;
                if(data) *data = MMT(x)->map[id].data;
            }
            //fprintf(stdout, "%s::%d rootid:%d tnodeid:%d id:%d\n",__FILE__, __LINE__, rootid, tnodeid, id);
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* get prev node key/data */
int mmtree_prev(void *x, int rootid, int tnodeid, int *key, int *data)
{
    int id = -1, parentid = 0;

    if(x && tnodeid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && tnodeid <  MMT(x)->state->total)
        {
            id = tnodeid;
            if(MMT(x)->map[id].left > 0)
            {
                id = MMT(x)->map[id].left;
                while(MMT(x)->map[id].right  > 0)
                {
                    id = MMT(x)->map[id].right;
                }
                //fprintf(stdout, "%s::%d id:%d\n", __FILE__, __LINE__, id);
            }
            else
            {
                while(id > 0)
                {
                    parentid = MMT(x)->map[id].parent;
                    if(MMT(x)->map[id].key > MMT(x)->map[parentid].key)
                    {
                        id = parentid;
                        goto end;
                    }
                    else
                    {
                        id = parentid;
                    }
                }
                //fprintf(stdout, "%s::%d id:%d\n", __FILE__, __LINE__, id);
            }
end:
            if(id > 0 && id < MMT(x)->state->total)
            {
                if(key)*key = MMT(x)->map[id].key;
                if(data)*data = MMT(x)->map[id].data;
            }
            //fprintf(stdout, "%s::%d rootid:%d tnodeid:%d id:%d\n",__FILE__, __LINE__, rootid, tnodeid, id);
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return id;
}

/* view node */
void mmtree_view_tnode(void *x, int tnodeid, FILE *fp)
{
    if(x)
    {
        if(MMT(x)->map[tnodeid].left > 0 && MMT(x)->map[tnodeid].left < MMT(x)->state->total)
        {
            mmtree_view_tnode(x, MMT(x)->map[tnodeid].left, fp);
        }
        fprintf(fp, "[%d:%d:%d]\n", tnodeid, MMT(x)->map[tnodeid].key, MMT(x)->map[tnodeid].data);
        if(MMT(x)->map[tnodeid].right > 0 && MMT(x)->map[tnodeid].right < MMT(x)->state->total)
        {
            mmtree_view_tnode(x, MMT(x)->map[tnodeid].right, fp);
        }
    }
    return ;
}

void mmtree_view_tree(void *x, int rootid, FILE *fp)
{
    if(x && rootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && rootid < MMT(x)->state->total)
        {
             mmtree_view_tnode(x, rootid, fp);
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return ;
}

/* set data */
int mmtree_set_data(void *x, int tnodeid, int data)
{
    int old = -1;

    if(x && tnodeid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && tnodeid < MMT(x)->state->total)
        {
            old = MMT(x)->map[tnodeid].data;
            MMT(x)->map[tnodeid].data = data;
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return old;
}

/* remove node */
void mmtree_remove(void *x, int *prootid, int tnodeid, int *key, int *data)
{
    int id = 0, pid = 0, parent = 0, child = 0, rid = 0, uid = 0, ppid = 0, 
        lid = 0, z = 0, color = 0;

    if(x && prootid && *prootid > 0 && tnodeid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        if(MMT(x)->map && MMT(x)->state && tnodeid < MMT(x)->state->total)
        {
            if(key) *key = MMT(x)->map[tnodeid].key;
            if(data) *data = MMT(x)->map[tnodeid].data;
            if(MMT(x)->map[tnodeid].left == 0)
                child = MMT(x)->map[tnodeid].right;
            else if(MMT(x)->map[tnodeid].right == 0)
                child = MMT(x)->map[tnodeid].left;
            else 
            {
                id = MMT(x)->map[tnodeid].right;
                while(MMT(x)->map[id].left > 0)
                    id = MMT(x)->map[id].left;
                parent = MMT(x)->map[id].parent;
                color = MMT(x)->map[id].color;
                if((child = MMT(x)->map[id].right) > 0)
                    MMT(x)->map[child].parent = parent;
                if((pid = MMT(x)->map[id].parent) > 0)
                {
                    if(MMT(x)->map[pid].left == id)
                        MMT(x)->map[pid].left = child;
                    else
                        MMT(x)->map[pid].right = child;
                }
                else
                {
                    *prootid = child;
                }
                if(MMT(x)->map[id].parent == tnodeid) parent = id;
                MMT(x)->map[id].color = MMT(x)->map[tnodeid].color;
                MMT(x)->map[id].parent = MMT(x)->map[tnodeid].parent;
                MMT(x)->map[id].left = MMT(x)->map[tnodeid].left;
                MMT(x)->map[id].right = MMT(x)->map[tnodeid].right;
                if((pid = MMT(x)->map[tnodeid].parent) > 0)
                {
                    if(MMT(x)->map[pid].left == tnodeid)
                        MMT(x)->map[pid].left = id;
                    else
                        MMT(x)->map[pid].right = id;
                }
                else
                {
                    *prootid = id;
                }
                lid = MMT(x)->map[tnodeid].left;
                MMT(x)->map[lid].parent = id;
                if((rid = MMT(x)->map[tnodeid].right) > 0)
                    MMT(x)->map[rid].parent = id;
                goto color_remove;
            }
            parent =  MMT(x)->map[tnodeid].parent;
            color = MMT(x)->map[tnodeid].color;
            if((id = child) > 0) MMT(x)->map[child].parent = parent;
            if((pid = MMT(x)->map[tnodeid].parent) > 0)
            {
                if(tnodeid == MMT(x)->map[pid].left) 
                    MMT(x)->map[pid].left = child;
                else 
                    MMT(x)->map[pid].right = child;
            }
            else *prootid = child;
            //remove color set
color_remove:
            if(color == MMT_COLOR_BLACK)
            {
                MMT_REMOVE_COLOR(x, prootid, child, parent, rid, rid, uid, ppid);
            }
            //add to qleft
            memset(&(MMT(x)->map[tnodeid]), 0, sizeof(MTNODE));
            if(MMT(x)->state->qleft == 0)
            {
                MMT(x)->state->qfirst = MMT(x)->state->qlast = tnodeid;
            }
            else
            {
                z = MMT(x)->state->qlast;
                MMT(x)->map[z].parent = tnodeid;
                MMT(x)->state->qlast = tnodeid;
            }
            MMT(x)->state->qleft++;
            MMT(x)->state->left++;
            //fprintf(stdout, "%s::%d %d start:%p state:%p map:%p current:%d left:%d total:%d qleft:%d qfirst:%d qlast:%d\n", __FILE__, __LINE__, id, MMT(x)->start, MMT(x)->state, MMT(x)->map, MMT(x)->state->current, MMT(x)->state->left, MMT(x)->state->total, MMT(x)->state->qleft, MMT(x)->state->qfirst, MMT(x)->state->qlast);
  
        }
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return ;
}

/* remove node */
void mmtree_remove_tnode(void *x, int tnodeid)
{
    int id = 0;

    if(x)
    {
        if(MMT(x)->map[tnodeid].left > 0 && MMT(x)->map[tnodeid].left < MMT(x)->state->total)
        {
            mmtree_remove_tnode(x, MMT(x)->map[tnodeid].left);
        }
        if(MMT(x)->map[tnodeid].right > 0 && MMT(x)->map[tnodeid].right < MMT(x)->state->total)
        {
            mmtree_remove_tnode(x, MMT(x)->map[tnodeid].right);
        }
        memset(&(MMT(x)->map[tnodeid]), 0, sizeof(MTNODE));
        if(MMT(x)->state->qleft == 0)
        {
            MMT(x)->state->qfirst = MMT(x)->state->qlast = tnodeid;
        }
        else
        {
            id = MMT(x)->state->qlast;
            MMT(x)->map[id].parent = tnodeid;
            MMT(x)->state->qlast = tnodeid;
        }
        MMT(x)->state->qleft++;
        MMT(x)->state->left++;
    }
    return ;
}

/* remove tree */
void mmtree_remove_tree(void *x, int rootid)
{
    if(x && rootid > 0)
    {
        MUTEX_LOCK(MMT(x)->mutex);
        mmtree_remove_tnode(x, rootid);
        //fprintf(stdout, "%s::%d rootid:%d start:%p state:%p map:%p current:%d left:%d total:%d qleft:%d qfirst:%d qlast:%d\n", __FILE__, __LINE__, rootid, MMT(x)->start, MMT(x)->state, MMT(x)->map, MMT(x)->state->current, MMT(x)->state->left, MMT(x)->state->total, MMT(x)->state->qleft, MMT(x)->state->qfirst, MMT(x)->state->qlast);
 
        MUTEX_UNLOCK(MMT(x)->mutex);
    }
    return ;
}

//close mmtree
void mmtree_close(void *x)
{
    if(x)
    {
        //fprintf(stdout, "%s::%d start:%p state:%p map:%p current:%d left:%d total:%d qleft:%d qfirst:%d qlast:%d sizeof(MTSTATE):%d\n", __FILE__, __LINE__, MMT(x)->start, MMT(x)->state, MMT(x)->map, MMT(x)->state->current, MMT(x)->state->left, MMT(x)->state->total, MMT(x)->state->qleft, MMT(x)->state->qfirst, MMT(x)->state->qlast, sizeof(MTSTATE));
        MMT_MUNMAP(x);
        MUTEX_DESTROY(MMT(x)->mutex);
        if(MMT(x)->fd) close(MMT(x)->fd);
        free(x);
    }
}


#ifdef _DEBUG_MMTREE
int main(int argc, char **argv) 
{
    void *mmtree = NULL;
    int i = 0, rootid = 0, id = 0, j = 0, next = 0, prev = 0, 
        old = 0, key = 0, data = 0;

    if((mmtree = mmtree_init("/tmp/test.mmtree")))
    {
        /*
        for(i = 1; i < 200; i++)
        {
            data = i - 1;
            id = mmtree_new_tree(mmtree, i, data);
            for(j = 1000; j > 0; j--)
            {
                old = 0;
                data = i * j;
                id = mmtree_insert(mmtree, id, j, data, &old);
                if(id == 100) fprintf(stdout, "key:%d data:%d\n", j, data);
            }
        }
        key = data = 0;
        mmtree_remove(mmtree, rootid, 100, &key, &data);
        fprintf(stdout, "key:%d data:%d\n", key, data);
        mmtree_view_tree(mmtree, 1, stdout);
        mmtree_remove_tree(mmtree, 1);
        //mmtree_view_tree(mmtree, 1, stdout);
        */
        int list[] = {123, 234, 345, 567, 98, 7, 45, 0, 240, 3, 5, 2, 1, 6, 8, 30, 23, 21, 43, 370};
        int key = 0, count = 0;
        rootid = mmtree_new_tree(mmtree, 0, data);
        for(j = 1; j < 1000000; j++)
        {
            old = -1;
            data = j;
            key = rand()%j;
            id = mmtree_insert(mmtree, &rootid, key, data, &old);
            if(id > 0 && old < 0) 
            {
                //fprintf(stdout, "j:%d id:%d key:%d rootid:%d\n", j, id, key, rootid);
                count++;
            }
            //fprintf(stdout, "tree:%d key:%d id:%d old:%d\n", rootid, list[j], id, old);
        }
        //mmtree_view_tree(mmtree, rootid, stdout);
        id = next = mmtree_min(mmtree, rootid, &key, &data);
        //fprintf(stdout, "tree:%d min:%d key:%d data:%d\n", rootid, id, key, data);
        do
        {
            fprintf(stdout, "tree:%d id:%d next:%d key:%d data:%d\n", rootid, id, next, key, data);
            id = next;
        }
        while((next = mmtree_next(mmtree, rootid, id, &key, &data)) > 0);
        fprintf(stdout, "count:%d\n", count);
        return 0;
        fprintf(stdout, "Removing rootid:%d key:%d data:%d\n", rootid, key, data);
        mmtree_remove(mmtree, &rootid, rootid, &key, &data);
        fprintf(stdout, "Removed rootid:%d key:%d data:%d\n", rootid, key, data);
        id = mmtree_max(mmtree, rootid, &key, &data);
        fprintf(stdout, "tree:%d max:%d key:%d data:%d\n", rootid, id, key, data);
        while((prev = mmtree_prev(mmtree, rootid, id, &key, &data)) > 0)
        {
            fprintf(stdout, "tree:%d id:%d prev:%d key:%d data:%d\n", rootid, id, prev, key, data);
            id = prev;
        }
        mmtree_close(mmtree);
    }
}
//gcc -o mmtree mmtree.c -D_DEBUG_MMTREE -g && ./mmtree
#endif
