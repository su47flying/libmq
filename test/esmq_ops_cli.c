

#include "libesmq_ops.h"

#define PATHNAME  "/home/lt_mq.txt"

int main (int argc, char *argv[])
{
    int ret ;
    struct lt_mq_info *mqinfo;
    char msg[512];
    unsigned long int limit = 1024 * 10;
    unsigned long int  count = 0 , priority;
    time_t s_time, e_time;
    
    if (argc == 2)
    {
       //type = atoi(argv[1]);
       limit = strtoul(argv[1], NULL, 10);
    }

    mqinfo = lt_mq_open(PATHNAME, O_CREAT, S_IRWXU, NULL);
    if (mqinfo == NULL)
    {
        printf("lt_mq_open error!\n");
        return -1;
    }     
   

    s_time = time(NULL);
  	//bzero(&msg, sizeof(msg));
    //snprintf(msg, 512, "prio:%lu www.sian.com.cn", priority); 
    while (count < limit )
    {
        if (1 == limit )
        {
           priority = 0;
        }
        else
        {
           priority = count + 1;
        }       
        bzero(&msg, sizeof(msg));
        snprintf(msg, 512, "prio:%lu www.sian.com.cn", priority);
        ret = lt_mq_send(mqinfo, msg, sizeof(msg), priority);
        if (ret != 0)
        {
            printf("lt_mq_send error! errno(%d)\n", lt_mq_get_errno());
            break;
        }
        count ++;
    }

    lt_mq_close(mqinfo);
    printf("count=%lu\n",count);
}

