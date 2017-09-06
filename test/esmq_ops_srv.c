#include "libesmq_ops.h"
#include "esprotocol_info.h"

#define PATHNAME  "/home/lt_mq.txt"

int main (int argc, char *argv[])
{
    int ret ,stop = 0, size ;
    struct lt_mq_info *mqinfo;
    struct es_http_info *http_info = NULL;
    char msg[2048];
    char file_name[256];
    unsigned long int  count = 0 ;
    time_t s_time, e_time;
    unsigned int priority, type;
    int err;
    struct lt_mq_attr	mq_attr = 
    { 
        .mq_flag = 1, 
        .mq_maxsize = 1024 *2, 
        .mq_maxmsg = 1024*10, 
        .mq_curmsgs = 0
    };

    bzero(file_name, sizeof(file_name));
    if(argc == 2)
    {
        strcpy(file_name, argv[1]);
    }
    else
    {
        strcpy(file_name, PATHNAME);
    }
    
    mqinfo = lt_mq_open(file_name, O_CREAT, S_IRWXU, &mq_attr);
    if (mqinfo == NULL)
    {
        printf("lt_mq_open error!\n");
        return -1;
    }         
    
    while ( stop == 0 )
    {
        bzero(&msg, sizeof(msg));
        size = lt_mq_recv(mqinfo, msg, sizeof(msg), priority, &type, 1);

        if (size <= 0)
        {
            size = lt_mq_get_errno();
            if(size != ES_MQ_ERR_TIMEOUT)
            {
                printf("lt_mq_recv error(%d) errno(%ld)!\n", ret, size);
                break;
            }
            else
            {
                //printf("count:%lu\n", count);
                continue;
            }
        }
        if (count == 0 )
        {
            s_time = time(NULL);
        }
        count ++;
        if (type == 0)
        {
            stop = 1;
        }
        else if(type == ES_HTTP_INFO)
        {
            http_info = (struct es_http_info *)msg;
            printf("seq:%lu\n", http_info->seq);
            printf("time:%lu\n", http_info->ts.tv_sec);
            printf("method:%s\n", http_info->method);
            printf("uri:%s\n", http_info->uri);
            printf("host:%s\n", http_info->host);
            printf("agent:%s\n", http_info->agent);
            printf("referer:%s\n", http_info->referer);
            printf("count:%lu\n", count);
        }
    }

    lt_mq_close(mqinfo);
}
