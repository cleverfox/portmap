#include <unistd.h>
#include <fcntl.h>
#include "portmap.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#define PM_PORT 1248

#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_error.h>

#define CLOCK_MONOTONIC 1000

static void  clock_gettime(int type, struct timespec *ts){
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts->tv_sec = mts.tv_sec;
    ts->tv_nsec = mts.tv_nsec;
}

#endif

double dtime(){
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC,&tv);
    double sec;
    sec=tv.tv_sec;
    sec+=(double)tv.tv_nsec/100000000;
    return sec;
}


int pm_sockread(struct portresolver *pr){
    int r=read(pr->fd,pr->buf+pr->offset,pr->buflen-pr->offset);
    if(r>0){
        pr->offset+=r;
    }else if(r==-1 && errno!=EAGAIN){
        return 0;
    }
    return r;
}

char *pm_readSockLine(struct portresolver *pr, int *res){
    if(pr->fd==-1){
        if(res) *res=-1;
        return NULL;
    }
    if(pr->start){
        memcpy(pr->buf,pr->buf+pr->start,pr->offset-pr->start);
        pr->offset-=pr->start;
        pr->start=0;
    }
    double start=dtime();
    while(1){
        double now=dtime();
        if(now-start>=pr->timeout){
            if(res) *res=-70;
            return NULL;
        }
        char *ptr=strstr(pr->buf,"\r\n");
        if(ptr){
            *ptr='\0';
            pr->start=ptr+2-pr->buf;
            if(res) *res=1;
            if(strcmp(pr->buf,"%END")==0){
                if(res) *res=-2;
                close(pr->fd);
                pr->fd=-1;
            }
            return pr->buf;
        }
        int r=pm_sockread(pr);
        if(r==0){
            close(pr->fd);
            pr->fd=-1;
            if(res) *res=-2;
            return NULL;
        }
        if(r==-1){
            if(pr->block){
                struct timespec slp;
                slp.tv_nsec=500000;
                slp.tv_sec=0;
                nanosleep(&slp,NULL);
            }else{
                if(res) *res=-3;
                return NULL;
            }
        }
    }
    if(res) *res=-4;
    return NULL;
}

int pm_setNonblocking(unsigned int sd) {
    unsigned int flags;
    if (-1 == (flags = fcntl(sd, F_GETFL, 0)))
        flags = 0;
    return fcntl(sd, F_SETFL, flags | O_NONBLOCK);
}

int pm_csconnect(struct sockaddr *ssin,size_t socklen){
    struct sockaddr_in6 *sin=(struct sockaddr_in6*) ssin;
    sin->sin6_port=htons(1248);

    int fdu = socket(sin->sin6_family, SOCK_STREAM, 0);  
    //char ster[128];
    if (fdu == -1) {  
        //strerror_r(errno,(char*)&ster,sizeof(ster));
        //printf("Error socket(): %s\n",(char*)&ster);
        return -1;  
    }  
    if (connect(fdu, ssin, socklen) == -1) {
        //strerror_r(errno,(char*)&ster,sizeof(ster));
        //printf("Error connect(): %s\n",(char*)&ster);
        return -1;
    }
    pm_setNonblocking(fdu);
    return fdu;
}

int pm_cconnect(char *address){
    struct sockaddr_in6 sin;

    struct in_addr q;
    if(inet_pton(AF_INET,address,&q.s_addr)==1){
        ((struct sockaddr_in *)&sin)->sin_addr=q;
        sin.sin6_family=AF_INET;
        sin.sin6_len=sizeof(struct sockaddr_in);
    }else if(inet_pton(AF_INET6,address,&sin.sin6_addr)==1){
        sin.sin6_family=AF_INET6;
        sin.sin6_len=sizeof(struct sockaddr_in6);
    }else{
        return -2;
    }
    return pm_csconnect((struct sockaddr*)&sin,sin.sin6_len);
}

int simpleresolve(char *host, char *service, struct pm_resolve *pms, int pmsc){
    int fdu=pm_cconnect(host);
    if(fdu<0)
        return fdu;
    return simpleresolve2(fdu,service,pms,pmsc);

}

int simpleresolve2(int fdu, char *service, struct pm_resolve *pms, int pmsc){
    struct portresolver pr;
    pr.fd=fdu;
    pr.block=1;
    pr.offset=0;
    pr.buflen=255;
    pr.start=0;
    pr.timeout=1;

    char *s=NULL;
    s=pm_readSockLine(&pr,NULL);

    int r;
    char a[255];
    sprintf(a,"LOOKUP %s\r\n",service);
    r=send(fdu,a,strlen(a),0);
    int rc=0;

    int res;
    while(1){
        char *s=pm_readSockLine(&pr,&res);
        if(res==-70)
            return -70;
        if(res==-3)
            break;
        if(res<0)
            break;
        char *token;
        int i=0;
        //printf("* %s\n",s);
        while ((token = strsep(&s, " ")) != NULL){
            switch(i){
                case 0:
                    if(*token!='S')
                        goto Out;
                case 1:
                    strcpy(pms[rc].servicename,token);
                    break;
                case 2:
                    strcpy(pms[rc].ipprotocol,token);
                    break;
                case 3:
                    pms[rc].port=atoi(token);
                    break;
                case 4:
                    strcpy(pms[rc].protoname,token);
                    break;
                case 5:
                    pms[rc].version=atoi(token);
                    break;
            }
            i++;
        }
        rc++;
Out:
        if(rc>=pmsc)
            break;

    }
    return rc;
}
