#ifndef __LIBESMQ_OPS_H__
#define __LIBESMQ_OPS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

#define	MQI_MAGIC	0x20130930

#define ES_MQ_VERSION       0x01010101  /* version */


#define ES_MQ_ERR_SEND_MSG_TOO_LONG   0x10
#define ES_MQ_ERR_RECV_MSG_TOO_SHORT  0x11
#define ES_MQ_ERR_GET_READ_BLOCK      0x12
#define ES_MQ_ERR_GET_WRITE_BLOCK     0x13
#define ES_MQ_ERR_TIMEOUT             0x14
#define ES_MQ_ERR_LOCK                0x15
#define ES_MQ_ERR_MAGIC               0x16
#define ES_MQ_ERR_ARGS                0x17



struct lt_mq_attr
{
    unsigned int mq_flag;
    unsigned int mq_maxsize;  /* max size of a message (in bytes) */
    unsigned int mq_maxmsg;   /* max number of messages allowed on queue */
    unsigned int mq_curmsgs;  /* number of messages currently on queue */
    u_int32_t mq_version;        /*  version of message queue */
};

struct lt_mq_w_hdr
{
    unsigned int mqwh_curmsgs;   /* number of messages currently on queue */ 
    unsigned int mqwh_free;    /* index of first free message */
    pthread_mutex_t	mqwh_lock;	/* mutex lock */
    pthread_cond_t	mqwh_wait;	/* and condition variable */
    pid_t		    mqwh_pid; /* owner of process id */
};

struct lt_mq_r_hdr
{
    unsigned int mqrh_curmsgs;   /* number of messages currently on queue */
    unsigned int mqrh_free;    /* index of first free message */
    pthread_mutex_t	mqrh_lock;	/* mutex lock */
    pthread_cond_t	mqrh_wait;	/* and condition variable */
    pid_t		    mqrh_pid; /* owner of process id */
};

struct lt_mq_hdr
{
    struct lt_mq_attr	mqh_attr;	/* the queue's attributes */
    //pid_t				mqh_pid;	[> nonzero PID if mqh_event set <]
    struct lt_mq_w_hdr mqh_w_hdr;
    struct lt_mq_r_hdr mqh_r_hdr;
    //pthread_mutex_t	mqh_lock;	/* mutex lock */
    //pthread_cond_t	mqh_wait;	/* and condition variable */
     
};

struct lt_msg_hdr
{
    unsigned int msg_num;
    unsigned int msg_prio; /* priority */
    unsigned int msg_type;     /* msg type */
    unsigned int msg_len;  /* actual length */
    unsigned int msg_next; /* index of next on linked list */
    unsigned char msg[0];  /*  */
};

struct lt_mq_info
{
    struct lt_mq_hdr   *mqi_hdr;	/* start of mmap'ed region */
    //struct lt_mq_w_hdr *mqi_w_hdr;
    //struct lt_mq_w_hdr *mqi_r_hdr;
    unsigned int mqi_magic;	/* magic number if open */
    pid_t	mqi_pid;				/* pid for this process */

};


int32_t lt_mq_state_checking(struct lt_mq_info * mq_info,u_int32_t timeout);
struct lt_mq_info *lt_mq_open (const char *pathname, int oflag, mode_t mode, struct lt_mq_attr *attr);
int lt_mq_send(struct lt_mq_info * mq_info, const char *ptr, unsigned int len, unsigned int prio, unsigned int type);
int lt_mq_recv(struct lt_mq_info * mq_info, unsigned char *ptr, unsigned int maxlen, unsigned int prio, unsigned int *type, int timeout);
int lt_mq_close(struct lt_mq_info * mq_info);
struct lt_mq_info *lt_mq_init (const char *pathname, struct lt_mq_attr *attr);

int lt_mq_get_errno();
#endif  //

