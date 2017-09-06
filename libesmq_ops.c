#include <dirent.h>

#include "libesmq_ops.h"
#include "es_debug.h"

struct lt_mq_attr	defattr = 
{ 
    .mq_flag = 1, 
    .mq_maxsize = 1024*4, 
    .mq_maxmsg = 1024*10, 
    .mq_curmsgs = 0
};

#define		MAX_TRIES	10	/* for waiting for initialization */
#define     MAX_MQ_SIG_SIZE   4096   /* max byte for one msg */
#define     MAX_MQ_MSG        10240  /* max msg count in the msg queue */

static int es_mq_errno = 0;
//es_debug_level = 6 ;
//es_debug_output = 1;

static int mq_init (struct lt_mq_info *info, struct lt_mq_attr *attr, void *mptr)
{
    struct lt_mq_hdr *mq_hdr = NULL;
    pthread_mutexattr_t	mattr;
	pthread_condattr_t	cattr;
    int ret = 0, i ;
    struct lt_msg_hdr *msg_hdr = NULL;
    unsigned int maxmsg = attr->mq_maxmsg ;
    
    mq_hdr = (struct lt_mq_hdr *)mptr;
    info->mqi_hdr = mq_hdr;
    mq_hdr->mqh_attr.mq_flag = attr->mq_flag;
    mq_hdr->mqh_attr.mq_maxsize = attr->mq_maxsize;
    mq_hdr->mqh_attr.mq_maxmsg = attr->mq_maxmsg;
    mq_hdr->mqh_attr.mq_curmsgs = 0;
    mq_hdr->mqh_attr.mq_version = ES_MQ_VERSION;
    
    /* write msg header */
    mq_hdr->mqh_w_hdr.mqwh_pid = 0;
    mq_hdr->mqh_w_hdr.mqwh_curmsgs = maxmsg; /**/
    mq_hdr->mqh_w_hdr.mqwh_free = 1 ;  /* point to the first msg */

    /* read msg header */
    mq_hdr->mqh_r_hdr.mqrh_pid = 0;
    mq_hdr->mqh_r_hdr.mqrh_curmsgs = 0 ;
    mq_hdr->mqh_r_hdr.mqrh_free = 0;    
    
    for ( i = 1; i <= maxmsg ; i++)
    {
        msg_hdr = (struct lt_msg_hdr *)(mptr + sizeof(struct lt_mq_hdr) + (i - 1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));
        msg_hdr->msg_num = i ;
        msg_hdr->msg_len = 0 ;
        msg_hdr->msg_prio = 0;
        msg_hdr->msg_type = 0;
        msg_hdr->msg_next = i + 1 ;
    }

    msg_hdr->msg_next = 0;  /* last msg */
    
    if ( (ret = pthread_mutexattr_init(&mattr)) != 0)
		return ret;

    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);     
    ret = pthread_mutex_init(&mq_hdr->mqh_w_hdr.mqwh_lock, &mattr);

	pthread_mutexattr_destroy(&mattr);	/* be sure to destroy */

    if ( (ret = pthread_condattr_init(&cattr)) != 0)
		return ret;

	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	ret = pthread_cond_init(&mq_hdr->mqh_w_hdr.mqwh_wait, &cattr);
	pthread_condattr_destroy(&cattr);	/* be sure to destroy */
    
    if ( (ret = pthread_mutexattr_init(&mattr)) != 0)
		return ret;

    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);     
    ret = pthread_mutex_init(&mq_hdr->mqh_r_hdr.mqrh_lock, &mattr);

	pthread_mutexattr_destroy(&mattr);	/* be sure to destroy */

    if ( (ret = pthread_condattr_init(&cattr)) != 0)
		return ret;

	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	ret = pthread_cond_init(&mq_hdr->mqh_r_hdr.mqrh_wait, &cattr);
	pthread_condattr_destroy(&cattr);	/* be sure to destroy */
    return 0;
}

struct lt_mq_info *lt_mq_open (const char *pathname, int oflag, mode_t mode, struct lt_mq_attr *attr)
{
    int fd, i, created = 0;
    unsigned int filesize ;
    struct stat	statbuff;
    struct lt_mq_info *mq_info = NULL; 
    void *mptr = NULL;
    
again:
    mode &= ~S_IXUSR;
    fd = open(pathname, oflag | O_EXCL | O_RDWR, mode | S_IXUSR); 
    if (fd < 0)
    {
        if (errno == EEXIST && (oflag & O_EXCL) == 0)
            goto exists;		/* already exists, OK */
        else
            return NULL;
    }
    created = 1;

    if (attr == NULL)
		attr = &defattr;
    
    filesize = sizeof(struct lt_mq_hdr) + (sizeof(struct lt_msg_hdr) + attr->mq_maxsize) * attr->mq_maxmsg;
    if (lseek(fd, filesize - 1, SEEK_SET) == -1)
        goto error;
    if (write(fd, "", 1) == -1)
        goto error;    
    
    mptr = mmap(NULL, filesize, PROT_READ | PROT_WRITE,	MAP_SHARED, fd, 0);
	if (mptr == MAP_FAILED)
	    goto error;
    
    if ( (mq_info = malloc(sizeof(struct lt_mq_info))) == NULL)
		goto error;

    mq_info->mqi_magic = MQI_MAGIC;
    mq_info->mqi_pid = getpid();
    if (mq_init(mq_info, attr, mptr) != 0)
        goto error;
    
    if ( fchmod(fd, mode) == -1 )  /* turn off  user-execute bit */
        goto error;

    close(fd);
    return mq_info;

exists:
    if ( (fd = open(pathname, O_RDWR)) < 0) 
    {
        if (errno == ENOENT && (oflag & O_CREAT))
            goto again;
        goto error;
    }

	for (i = 0; i < MAX_TRIES; i++) 
    {
		if (stat(pathname, &statbuff) == -1)
        {
			if (errno == ENOENT && (oflag & O_CREAT)) 
            {
				close(fd);
				goto again;
			}
			goto error;
		}
		if ((statbuff.st_mode & S_IXUSR) == 0)
			break;
		sleep(1);
	}
    
    filesize = statbuff.st_size;
    mptr = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mptr == MAP_FAILED)
        goto error;
    close(fd);

    if ( (mq_info = malloc(sizeof(struct lt_mq_info))) == NULL)
    {
        ES_DEBUG(LOG_ERR,"mallco error!\n");
		goto error;
	}
    mq_info->mqi_magic = MQI_MAGIC;
    mq_info->mqi_pid = getpid();
    mq_info->mqi_hdr = (struct lt_mq_hdr *)mptr;

	if(mq_info->mqi_hdr->mqh_attr.mq_version != ES_MQ_VERSION)
	{
	    ES_DEBUG(LOG_ERR,"mq version error(0x%x:0x%x)!\n", mq_info->mqi_hdr->mqh_attr.mq_version, ES_MQ_VERSION);
        unlink(pathname); /* nmap file was error! */
        goto error;
	}
    return mq_info;

error:
    if (created)
		unlink(pathname);
    if (mptr != MAP_FAILED)
		munmap(mptr, filesize);
	if (mq_info != NULL)
		free(mq_info);
	close(fd);
    return NULL;
}
static int lt_mq_get_r_num(struct lt_mq_info * mq_info, unsigned int *num, int timeout)
{
    int ret = 0;
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;
    unsigned int msg_num = 0;
    struct lt_msg_hdr *msghdr;
    struct lt_mq_r_hdr *mqr_hdr;
    struct lt_mq_attr *attr;
    struct timespec ts;
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    mqr_hdr = &mqhdr->mqh_r_hdr;
    attr = &mqhdr->mqh_attr;

    *num = 0;
    if (( pthread_mutex_lock(&mqr_hdr->mqrh_lock)) != 0) 
    {
        ES_DEBUG(LOG_ERR, "pthread_mutex_lock error\n");
        return -1;
    }
    mqr_hdr->mqrh_pid = mq_info->mqi_pid ; /* set owner pid */
    
    while (mqr_hdr->mqrh_curmsgs == 0)
    {
        if(timeout == 0)
        {
            pthread_cond_wait(&mqr_hdr->mqrh_wait, &mqr_hdr->mqrh_lock);
        }
        else
        {
            /*  FIXME  handle interrupter */
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout;
            ret = pthread_cond_timedwait(&mqr_hdr->mqrh_wait, &mqr_hdr->mqrh_lock, &ts);
            if( ret == ETIMEDOUT)
            {
                ES_DEBUG(LOG_INFO, "pthread_cond_timedwait timeout.\n");
                goto out;
            }
        }
    }
/*
    if(ret != 0)
    {
        ES_DEBUG(LOG_ERR,"ret:%d\n", ret);
        goto out;
    }
    */
    msg_num = mqr_hdr->mqrh_free;

    if (msg_num < 1)
    {
        //pthread_mutex_unlock(&mqr_hdr->mqrh_lock);
        ES_DEBUG(LOG_ERR,"msg_num %d error\n", msg_num);
        ret = -3;
        goto out;
    }
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));
    mqr_hdr->mqrh_free = msghdr->msg_next;
    mqr_hdr->mqrh_curmsgs -- ;

out:    
    mqr_hdr->mqrh_pid = 0; /* unset owner pid */
    pthread_mutex_unlock(&mqr_hdr->mqrh_lock);
    *num = msg_num;
    return ret;
}

static int lt_mq_get_w_num(struct lt_mq_info * mq_info, unsigned int *num)
{
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;
    unsigned int msg_num = 0;
    struct lt_msg_hdr *msghdr;
    struct lt_mq_w_hdr *mqw_hdr;
    struct lt_mq_attr *attr;
    int32_t ret = 0;
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    mqw_hdr = &mqhdr->mqh_w_hdr;
    attr = &mqhdr->mqh_attr;

    *num = 0 ;
    if (( pthread_mutex_lock(&mqw_hdr->mqwh_lock)) != 0) 
    {
        ES_DEBUG(LOG_ERR,"pthread_mutex_lock error\n");
        es_mq_errno = ES_MQ_ERR_LOCK;
        return -1;
    }

    mqw_hdr->mqwh_pid = mq_info->mqi_pid;
    
    while (mqw_hdr->mqwh_curmsgs == 0)
        pthread_cond_wait(&mqw_hdr->mqwh_wait, &mqw_hdr->mqwh_lock);

    msg_num = mqw_hdr->mqwh_free;

    if (msg_num < 1)
    {
        //pthread_mutex_unlock(&mqw_hdr->mqwh_lock);
        ES_DEBUG(LOG_ERR,"msg_num %d error. mqwh_curmsgs=%lu\n", msg_num, mqw_hdr->mqwh_curmsgs);
        ret = -1;
        goto out;
    }
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));
    mqw_hdr->mqwh_free = msghdr->msg_next;
    mqw_hdr->mqwh_curmsgs -- ;

out:
    mqw_hdr->mqwh_pid = 0;
    pthread_mutex_unlock(&mqw_hdr->mqwh_lock);
    *num = msg_num;
    return ret;
}

int lt_mq_insert_w_list(struct lt_mq_info * mq_info, unsigned int msg_num)
{
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;    
    struct lt_msg_hdr *msghdr;
    struct lt_mq_w_hdr *mqw_hdr;
    struct lt_mq_attr *attr;
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    mqw_hdr = &mqhdr->mqh_w_hdr;
    attr = &mqhdr->mqh_attr;

    if (( pthread_mutex_lock(&mqw_hdr->mqwh_lock)) != 0) 
    {
        ES_DEBUG(LOG_ERR,"pthread_mutex_lock error\n");
        es_mq_errno = ES_MQ_ERR_LOCK;
        return -1;
    }

    mqw_hdr->mqwh_pid = mq_info->mqi_pid;
    
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));
    msghdr->msg_next = mqw_hdr->mqwh_free;
    mqw_hdr->mqwh_free = msg_num;
    
    if (mqw_hdr->mqwh_curmsgs == 0)
        pthread_cond_broadcast(&mqw_hdr->mqwh_wait);
    
    mqw_hdr->mqwh_curmsgs ++;
    mqw_hdr->mqwh_pid = 0;
    pthread_mutex_unlock(&mqw_hdr->mqwh_lock);

    return 0;
}

int lt_mq_insert_r_list(struct lt_mq_info * mq_info, unsigned int msg_num)
{
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;    
    struct lt_msg_hdr *msghdr;
    struct lt_mq_r_hdr *mqr_hdr;
    struct lt_mq_attr *attr;
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    mqr_hdr = &mqhdr->mqh_r_hdr;
    attr = &mqhdr->mqh_attr;

    if (( pthread_mutex_lock(&mqr_hdr->mqrh_lock)) != 0) 
    {
        ES_DEBUG(LOG_ERR,"pthread_mutex_lock error\n");
        es_mq_errno = ES_MQ_ERR_LOCK;
        return -1;
    }

    mqr_hdr->mqrh_pid = mq_info->mqi_pid;
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));
    msghdr->msg_next = mqr_hdr->mqrh_free;
    mqr_hdr->mqrh_free = msg_num;
    
    if (mqr_hdr->mqrh_curmsgs == 0)
        pthread_cond_signal(&mqr_hdr->mqrh_wait);

    mqr_hdr->mqrh_curmsgs ++;
    mqr_hdr->mqrh_pid = 0;
    pthread_mutex_unlock(&mqr_hdr->mqrh_lock);

    return 0;
}

int lt_mq_send(struct lt_mq_info * mq_info, const char *ptr, unsigned int len, unsigned int prio, unsigned int type)
{
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;
    unsigned int msg_num = 0;
    struct lt_msg_hdr *msghdr;
    struct lt_mq_attr *attr ;
    struct lt_mq_w_hdr *mqwhdr;
    struct lt_mq_r_hdr *mqrhdr;

    if(!mq_info)
    {
        ES_DEBUG(LOG_ERR,"mq_info is NULL\n");
        es_mq_errno = ES_MQ_ERR_ARGS;
        return -1;
    }
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    attr = &mqhdr->mqh_attr;
    mqwhdr = &mqhdr->mqh_w_hdr;
    mqrhdr = &mqhdr->mqh_r_hdr;
    
    if (mq_info->mqi_magic != MQI_MAGIC)
    {
        ES_DEBUG(LOG_ERR,"magic(0x%x) error\n", mq_info->mqi_magic);
        es_mq_errno = ES_MQ_ERR_MAGIC;
        return -1;
    }

    if (len > attr->mq_maxsize)
    {
        ES_DEBUG(LOG_ERR,"msg too long(%lu, %lu)\n", len, attr->mq_maxsize);
        es_mq_errno = ES_MQ_ERR_SEND_MSG_TOO_LONG;
        return -1;
    }

    lt_mq_get_w_num(mq_info, &msg_num);

    if(msg_num < 1)
        return -1;
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));

    msghdr->msg_len = len;
    memcpy(msghdr->msg, ptr, len);
    msghdr->msg_prio = prio;
    msghdr->msg_type = type;
    msghdr->msg_next = 0 ;
    return lt_mq_insert_r_list(mq_info, msg_num);
    
}

int lt_mq_recv(struct lt_mq_info * mq_info, unsigned char *ptr, unsigned int maxlen, unsigned int prio, unsigned int *type, int timeout)
{
    int ret = 0;
    struct lt_mq_hdr *mqhdr = NULL;
    unsigned char *mptr = NULL;
    unsigned int msg_num = 0, msg_len;
    struct lt_msg_hdr *msghdr;
    struct lt_mq_attr *attr ;
    struct lt_mq_w_hdr *mqwhdr;
    struct lt_mq_r_hdr *mqrhdr;

    if(!mq_info)
    {
        ES_DEBUG(LOG_ERR,"mq_info is NULL\n");
        es_mq_errno = ES_MQ_ERR_ARGS;
        return -1;
    }
    
    mqhdr = mq_info->mqi_hdr;
    mptr = (unsigned char *)mqhdr;
    attr = &mqhdr->mqh_attr;
    mqwhdr = &mqhdr->mqh_w_hdr;
    mqrhdr = &mqhdr->mqh_r_hdr;
    
    if (mq_info->mqi_magic != MQI_MAGIC)
    {
        ES_DEBUG(LOG_ERR,"magic(0x%x) error\n", mq_info->mqi_magic);
        es_mq_errno = ES_MQ_ERR_MAGIC;
        return -1;
    }

    if (maxlen < attr->mq_maxsize)
    {
        //ES_DEBUG(LOG_WARNING,"recv buff(%lu) less than msg(%lu)\n", maxlen, attr->mq_maxsize);
        //return -1; 
    }
    ret = lt_mq_get_r_num(mq_info,  &msg_num, timeout);
    
    if(ret != 0)
    {
        if(ret == ETIMEDOUT)
        {
            es_mq_errno = ES_MQ_ERR_TIMEOUT;
        }
        else
        {
            ES_DEBUG(LOG_ERR,"lt_mq_get_r_num ret:%d\n", ret);
            es_mq_errno = ES_MQ_ERR_GET_READ_BLOCK;
        }
        return -1;
    }
    msghdr = (struct lt_msg_hdr*)(mptr + sizeof(struct lt_mq_hdr) + (msg_num -1) * (sizeof(struct lt_msg_hdr) + attr->mq_maxsize));

    if(msghdr->msg_len <= maxlen)
    {
        memcpy(ptr, msghdr->msg, msghdr->msg_len);
        *type = msghdr->msg_type;
        msg_len = msghdr->msg_len;
        bzero(msghdr->msg, msghdr->msg_len);
        msghdr->msg_len = 0 ;
        msghdr->msg_prio = 0 ;
        msghdr->msg_type = 0;
        msghdr->msg_next = 0 ;
        ret = lt_mq_insert_w_list(mq_info, msg_num);
        if(ret == 0)
            return msg_len;
        else
           return -1;
    }
    else
    {
        ES_DEBUG(LOG_ERR,"recv buff(%lu) less than msg(%lu)\n", maxlen, msghdr->msg_len);
        lt_mq_insert_r_list(mq_info,msg_num);
        es_mq_errno = ES_MQ_ERR_RECV_MSG_TOO_SHORT;
        return -1;
    }
    
}

int lt_mq_get_errno()
{
    return es_mq_errno;
}

int lt_mq_close(struct lt_mq_info * mq_info)
{
    struct lt_mq_hdr *mqhdr = NULL;
    struct lt_mq_attr *attr = NULL;
    unsigned int filesize = 0 ;

    if(!mq_info)
    {
        ES_DEBUG(LOG_ERR,"mq_info is NULL\n");
        return -1;
    }
    if (mq_info->mqi_magic != MQI_MAGIC)
    {
        return -1;
    }

    mqhdr = mq_info->mqi_hdr;
	attr = &mqhdr->mqh_attr;

    filesize = sizeof(struct lt_mq_hdr) + (sizeof(struct lt_msg_hdr) + attr->mq_maxsize) * attr->mq_maxmsg;
    if (munmap(mq_info->mqi_hdr, filesize) == -1)
		return(-1);
    mq_info->mqi_magic = 0;		/* just in case */
	free(mq_info);

    return 0;
}

struct lt_mq_info *lt_mq_init (const char *pathname, struct lt_mq_attr *attr)
{
    return lt_mq_open(pathname, O_CREAT, S_IRWXU, attr);
}

static int32_t mq_check_process(pid_t pid)
{
    int ret = 0 ;
    struct dirent *dp;
    DIR *dir;
    char path[256];

    bzero(path, sizeof(path));

    snprintf(path, sizeof(path), "%s/%u", "/proc", pid);
    dir = opendir(path);
    if(dir == NULL)
    {        
        return 0;
    }

    closedir(dir);

    return 1;
}


static int32_t __mq_checking_lock(pid_t pid, pthread_mutex_t *lock, u_int32_t timeout)
{
    int32_t ret,i, count = 3;
    struct timespec ts;
    /* checking readlock */
    for(i = 0; i < count; i++)
    {
        /*  FIXME  handle interrupter */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout;
        ret = pthread_mutex_timedlock(lock, &ts);
        if(ret == 0)
        {
            pthread_mutex_unlock(lock);
            break;
        }
        else
        {
            ES_DEBUG(LOG_WARNING, "pthread_mutex_timedlock ret:%d\n", ret);
            if(i == (count -1))
            {
                if(mq_check_process(pid) == 0) /* pid was not exist */
                {
                    pthread_mutex_unlock(lock);
                    ES_DEBUG(LOG_ERR, "deadlock!\n");
                }
                else
                {
                    ES_DEBUG(LOG_WARNING, "lock used too long time\n");
                }
                break;
            }
        }
    }
    
    return 0;
}

static int32_t mq_checking_lock(struct lt_mq_info * mq_info,u_int32_t timeout)
{    
    struct lt_msg_hdr *msghdr;
    struct lt_mq_w_hdr *mqw_hdr;
    struct lt_mq_r_hdr *mqr_hdr;
    pthread_mutex_t *lock;
    pid_t pid;
    if(mq_info == NULL)
    {
       ES_DEBUG(LOG_ERR, "mq_info is NULL\n");
       return -1;
    }

    mqr_hdr = &mq_info->mqi_hdr->mqh_r_hdr;
    mqw_hdr = &mq_info->mqi_hdr->mqh_w_hdr;

    /* read list lock */
    lock = &mqr_hdr->mqrh_lock;
    pid = mqr_hdr->mqrh_pid;
    __mq_checking_lock(pid, lock, timeout);

    /* write list lock */
    lock = &mqw_hdr->mqwh_lock;
    pid = mqw_hdr->mqwh_pid;
    __mq_checking_lock(pid, lock, timeout);


    return 0;
}
/*
* this func was be used for checing mq lock state.  
* it was be called in a thread.
*/
int32_t lt_mq_state_checking(struct lt_mq_info * mq_info,u_int32_t timeout)
{
    if(mq_info == NULL)
    {
        ES_DEBUG(LOG_ERR, "mq_info is NULL\n");
        return -1;
    }

    return mq_checking_lock(mq_info, timeout);
}

__attribute__((constructor)) void esmq_init()
{
    es_debug_level = LOG_NOTICE;
    es_debug_output = ES_OUTPUT_FIEL;
    //printf("debug level:%d output:%d\n", es_debug_level, es_debug_output);
}

