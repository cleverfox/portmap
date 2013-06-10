#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <event2/event.h>
#include <netdb.h>
#include "portmap.h"

#define LOG_WRITE(loglevel, ...) {printf(__VA_ARGS__);printf("\n");}
struct client {
    int fd;
    uint16_t rlen;
    char rbuf[256];
    uint16_t offset;
    struct event *e;
    char tos;
};
struct connection {
    int fd;
    uint16_t rlen;
    uint8_t *rbuf;
    uint16_t offset;
    struct event *e;
};
struct service {
    uint16_t port;
    uint16_t protocol;
    uint8_t version;
    char protoname[16];
    struct connection *owner;
    uint8_t matchany;
    TAILQ_ENTRY(service) entries;
    char servicename[];
};
struct {
    int fd4;
    int fd6;
    int fdu;
    struct event *e4;
    struct event *e6;
    struct event *eu;
    struct sockaddr_in  sin4;
    struct sockaddr_in6 sin6;
    struct sockaddr_un  sinu;
    struct event_base *loop;
    TAILQ_HEAD(servicetailq,service) services;
} pm;

char banner[]="MiniPortmap version 0.1\r\n";

void handleu(evutil_socket_t fd, short what, void *arg){
    struct connection *c=arg;
    printf("Got an event on service socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
    if(!c->rbuf){
        uint16_t lenbuf;
        size_t len=read(fd,&lenbuf,2); 
        if(len==0 || len!=2){
            event_del(c->e);
            close(fd);
            free(c->e);

            struct service *s,*s1;
            TAILQ_FOREACH_SAFE(s,&pm.services,entries,s1){
                if(s->owner==c){
                    TAILQ_REMOVE(&pm.services,s,entries);
                    free(s);
                }
            }

            free(c);
            if(len==0)
                printf("Connection finished on fd %d\n",fd);
            else
                printf("Connection error on fd %d. Can't read message length. Connection closed\n",fd);
            return;
        }
        c->rlen=htons(lenbuf);
        printf("Read %ld bytes. expected %d\n",len,2);
        uint8_t *buf=malloc(c->rlen+1);
        bzero(buf,c->rlen+1);
        memcpy(buf,&lenbuf,2);
        c->rbuf=buf;
        c->offset=2;
    }
    ssize_t len=read(fd,c->rbuf+c->offset,c->rlen-c->offset); 
    if(len<0){
        event_del(c->e);
        close(fd);
        free(c->e);
        free(c->rbuf);
        free(c);
        printf("Connection closed on fd %d\n",fd);
        return;
    }
    printf("Read %ld bytes. expected %d\n",len,c->rlen-c->offset);
    c->offset+=len;
    if(c->offset==c->rlen){
        printf("Got complete message\n");
        struct assign *as=(struct assign*)c->rbuf;
        struct protoent *p=getprotobynumber(as->ipprotocol);
        printf("Setup service %s at port %s %d with protocol %s version %d\n",
                as->servicename,p->p_name,htons(as->port),as->protoname,as->version);
        int slen=sizeof(struct service)+strlen(as->servicename)+1;
        struct service *s=malloc(slen);
        bzero(s,slen);
        s->port=ntohs(as->port);
        s->protocol=(as->ipprotocol);
        if(as->servicename[0]=='.'){
            strcpy(s->servicename,as->servicename+1);
            s->matchany=0;
        }else{
            strcpy(s->servicename,as->servicename);
            s->matchany=1;
        }
        if(strlen(as->protoname)<1)
            strcpy(s->protoname, "_");
        else
            strcpy(s->protoname, as->protoname);
        s->version=as->version;
        s->owner=c;
        TAILQ_INSERT_TAIL(&pm.services,s,entries);
        free(c->rbuf);
        c->rbuf=NULL;
        c->rlen=0;
        c->offset=0;
    }
}

void handlec(evutil_socket_t fd, short what, void *arg){
    struct client *c=arg;
    printf("Got an event on client socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");

    if(what&EV_TIMEOUT){
        c->tos--;
        if(c->tos<=0){
            event_del(c->e);
            close(fd);
            free(c->e);
            free(c);
            printf("Idle timeout. Connection finished on fd %d\n",fd);
            return;
        }
    }
    if(what&EV_READ){
        c->tos=2;
        size_t len=read(fd,c->rbuf,c->rlen-c->offset); 
        if(len==0){
            event_del(c->e);
            close(fd);
            free(c->e);
            free(c);
            printf("Connection finished on fd %d\n",fd);
            return;
        }
        printf("Read %ld bytes. expected %d\n",len,c->rlen-c->offset);
        c->offset+=len;
        char *fs;
        while((fs=strstr((const char*)c->rbuf,"\r\n"))!=NULL){
            *fs='\0';
            char slen=(long)fs-(long)c->rbuf;
            printf("Received %s %d\n",c->rbuf,slen);
            if(strncasecmp(c->rbuf,"lookup ",7)==0){
                //perform lookup
                char *pattern=c->rbuf+7;
                if(*pattern=='*')
                    pattern=NULL;
                struct service *s;
                char obuf[255];
                TAILQ_FOREACH(s,&pm.services,entries){
                    if(pattern){
                        if(strcmp(pattern,s->servicename)!=0) continue;
                    }else{
                        if(!s->matchany) continue;
                    }

                    struct protoent *p=getprotobynumber(s->protocol);
                    sprintf(obuf,"S %s %s %d %s %d\r\n",
                            s->servicename,p->p_name,s->port,s->protoname,s->version);
                    send(fd,obuf,strlen(obuf),0);
                }
                send(fd,"%END\r\n\r\n",8,0);
            }
            strcpy(c->rbuf,fs+2);
            c->offset-=2+slen;
        }
    }
}

void acceptu(evutil_socket_t fd, short what, void *arg){
    printf("Got an event on listen unix socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
    int cfd=accept(fd,NULL,NULL);
    struct connection *con=malloc(sizeof(struct connection));
    bzero(con,sizeof(struct connection));
    con->fd=cfd;
    con->e=event_new(pm.loop, cfd, EV_READ|EV_PERSIST, handleu, con);
    event_add(con->e,NULL);
    printf("Service accepted fd %d\n",cfd);
};

void acceptc(evutil_socket_t fd, short what, void *arg){
    printf("Got an event on listen inet socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
    int cfd=accept(fd,NULL,NULL);
    
    struct client *con=malloc(sizeof(struct client));
    bzero(con,sizeof(struct client));
    con->fd=cfd;
    con->rlen=255;
    con->e=event_new(pm.loop, cfd, EV_READ|EV_PERSIST, handlec, con);
    struct timeval tv;
    tv.tv_sec=2;
    tv.tv_usec=0;
    con->tos=2;
    event_add(con->e,&tv);
    write(cfd,banner,strlen(banner));
    printf("Client accepted fd %d\n",cfd);
};

int listenu(){
    pm.fdu = socket(PF_UNIX, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fdu == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error socket(): %s",(char*)&ster);
        return -1;  
    }  
    int on = 1; 
    if (setsockopt(pm.fdu, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    unlink(pm.sinu.sun_path);
    if (bind(pm.fdu, (const struct sockaddr *)&pm.sinu, sizeof(struct sockaddr_un)) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error bind(): %s",(char*)&ster);
        return -1;
    }
    if (listen(pm.fdu, 64/*backlog*/) == -1) {  
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    pm.eu=event_new(pm.loop, pm.fdu, EV_READ|EV_PERSIST, acceptu, NULL);
    event_add(pm.eu,NULL);
    return 0;
}

int listen4(){
    pm.fd4 = socket(PF_INET, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fd4 == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error socket(): %s",(char*)&ster);
        return -1;  
    }  
    int on = 1; 
    if (setsockopt(pm.fd4, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    if (bind(pm.fd4, (const struct sockaddr *)&pm.sin4, sizeof(struct sockaddr_in)) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        char pbuf[64];
        inet_ntop(AF_INET,&pm.sin4.sin_addr ,pbuf,64);
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error bind(): %s on %s:%d",(char*)&ster,pbuf,ntohs(pm.sin4.sin_port));  
        return -1;
    }
    if (listen(pm.fd4, 64/*backlog*/) == -1) {  
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    pm.e4=event_new(pm.loop, pm.fd4, EV_READ|EV_PERSIST, acceptc, NULL);
    event_add(pm.e4,NULL);
    return 0;
}

int listen6(){
    pm.fd6 = socket(PF_INET6, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fd6 == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error socket(): %s",(char*)&ster);
        return -2;  
    }  
    int on = 1; 
    if (setsockopt(pm.fd6, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    if (setsockopt(pm.fd6, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0){
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error setsockopt(): %s",(char*)&ster);  
        return -3;
    }
    if (bind(pm.fd6, (struct sockaddr*)&pm.sin6, sizeof(struct sockaddr_in6)) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        char pbuf[64];
        inet_ntop(AF_INET6,&pm.sin6.sin6_addr ,pbuf,64);
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error bind(): %s on %s:%d",(char*)&ster,pbuf,ntohs(pm.sin6.sin6_port));  
        return -1;
    }
    
    if (listen(pm.fd6, 64/*backlog*/) == -1) {  
        LOG_WRITE(LF_SERVER|LL_ERROR,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    pm.e6=event_new(pm.loop, pm.fd6, EV_READ|EV_PERSIST, acceptc, NULL);
    event_add(pm.e6,NULL);
    return 0;
}

int main(int argc, char *argv[]){
    LOG_WRITE(0,"Hello");
    TAILQ_INIT(&pm.services);
    pm.loop = event_base_new();
    pm.sin4.sin_family=AF_INET;
    pm.sin4.sin_addr.s_addr=INADDR_ANY;
    pm.sin4.sin_port=htons(1248);
    pm.sin6.sin6_family=AF_INET6;
    pm.sin6.sin6_addr=in6addr_any;
    pm.sin6.sin6_port=htons(1248);
    pm.sinu.sun_family=AF_UNIX;
    sprintf(pm.sinu.sun_path,"/tmp/portmap.1248");
    pm.sinu.sun_len=strlen(pm.sinu.sun_path);
    LOG_WRITE(0,"%d",listen4());
    LOG_WRITE(0,"%d",listen6());
    LOG_WRITE(0,"%d",listenu());
    event_base_dispatch(pm.loop);
    return 0;
}
