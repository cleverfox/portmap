#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#include <event2/event.h>
#include "portmap.h"

struct portresolver {
    int fd;
    int block;
    int start;
    int offset;
    int buflen;
    char buf[256];
};

int sockread(struct portresolver *pr){
    int r=read(pr->fd,pr->buf+pr->offset,pr->buflen-pr->offset);
    if(r>0){
        pr->offset+=r;
    }else if(r==-1 && errno!=EAGAIN){
        return 0;
    }
    return r;
}

char *readSockLine(struct portresolver *pr, int *res){
    if(pr->fd==-1){
        if(res) *res=-1;
        return NULL;
    }
    if(pr->start){
        memcpy(pr->buf,pr->buf+pr->start,pr->offset-pr->start);
        pr->offset-=pr->start;
        pr->start=0;
    }
    while(1){
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
        int r=sockread(pr);
        if(r==0){
            close(pr->fd);
            pr->fd=-1;
            if(res) *res=-2;
            return NULL;
        }
        if(r==-1){
            if(pr->block){
                struct timespec slp;
                slp.tv_nsec=50000000;
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

int setNonblocking(unsigned int sd) {
    unsigned int flags;
    if (-1 == (flags = fcntl(sd, F_GETFL, 0)))
        flags = 0;
    return fcntl(sd, F_SETFL, flags | O_NONBLOCK);
}


/*
void dumpRes(){
    char* p=strstr(a,"\r\n");
    if(!p)
        p=a;
    else
        p+=2;
    a[offset]='\0';
    printf("%s",p);
}
*/

void doread(evutil_socket_t fd, short what, void *arg){
    sockread(arg);
    int res;
    while(1){
        char *s=readSockLine(arg,&res);
        if(res==-3)
            break;
        if(res<0)
            exit(0);
        printf("%s\n",s);
    }
}


int main(int argc, char *argv[]){
    if(argc<3){
        printf("min 2 args: <host> <service name>\n");
        exit(10);
    }
    struct sockaddr_in6 sin;

        struct in_addr q;
        if(inet_pton(AF_INET,argv[1],&q.s_addr)==1){
                ((struct sockaddr_in *)&sin)->sin_addr=q;
                sin.sin6_family=AF_INET;
            sin.sin6_len=sizeof(struct sockaddr_in);
        }else if(inet_pton(AF_INET6,argv[1],&sin.sin6_addr)==1){
            sin.sin6_family=AF_INET6;
            sin.sin6_len=sizeof(struct sockaddr_in6);
        }else{
            printf("Can't parse ip address %s",argv[1]);
        }

    sin.sin6_port=htons(1248);

    int fdu = socket(sin.sin6_family, SOCK_STREAM, 0);  
    char ster[128];
    if (fdu == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        printf("Error socket(): %s\n",(char*)&ster);
        return -1;  
    }  
    if (connect(fdu, (const struct sockaddr *)&sin, sin.sin6_len) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        printf("Error connect(): %s\n",(char*)&ster);
        return -1;
    }
    /*
    int len=sizeof(struct assign)+strlen(argv[1])+1;
    struct assign *a=alloca(len);
    bzero(a,len);
    a->length=htons(len);
    strcpy(a->servicename,argv[1]);
    uint16_t portnum=strtol(argv[2], (char **)NULL, 10);
    a->port=htons(portnum);
    a->magic_number=htons(PM_SETUP);
    struct protoent * p=getprotobyname("tcp");
    a->ipprotocol=p->p_proto;
    */

    struct event_base *loop;
    loop = event_base_new();
    event_base_dispatch(loop);

    setNonblocking(fdu);
    struct portresolver pr;
    pr.fd=fdu;
    pr.block=0;
    pr.offset=0;
    pr.buflen=255;
    pr.start=0;

    char *s=NULL;
    pr.block=1;
    s=readSockLine(&pr,NULL);

    pr.block=0;
    int r;
    char a[255];
    sprintf(a,"LOOKUP %s\r\n",argv[2]);
    r=send(fdu,a,strlen(a),0);
    //printf("Request sent: %d\n",r);

    struct event *e=event_new(loop, fdu, EV_READ|EV_PERSIST, doread, &pr);
    event_add(e,NULL);
    event_base_dispatch(loop);
    return 0;
}

