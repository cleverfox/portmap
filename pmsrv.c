#include <stdio.h>
#include <signal.h>
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
#include <syslog.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "portmap.h"

//#define LOG_WRITE(loglevel, ...) {printf(__VA_ARGS__);printf("\n");}
#define LOG_WRITE(loglevel, ...) {syslog(LOG_WARNING,__VA_ARGS__);if(loglevel){printf(__VA_ARGS__);printf("\n");}}
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
    /*
    LOG_WRITE(0,"Got an event on service socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
            */
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
            if(len==0){
            //    LOG_WRITE(0,"Connection finished on fd %d\n",fd);
            }else
                LOG_WRITE(0,"Connection error on fd %d. Can't read message length. Connection closed\n",fd);
            return;
        }
        c->rlen=(lenbuf);
        //printf("Read %ld bytes. expected %d\n",len,2);
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
        //printf("Connection closed on fd %d\n",fd);
        return;
    }
    //printf("Read %ld bytes. expected %d\n",len,c->rlen-c->offset);
    c->offset+=len;
    if(c->offset==c->rlen){
        struct assign *as=(struct assign*)c->rbuf;
        LOG_WRITE(0,"Got complete message %x\n",as->magic_number);
        if(as->magic_number==PM_SETUP){
            struct protoent *p=getprotobynumber(as->ipprotocol);
            struct service *ts;
            TAILQ_FOREACH(ts,&pm.services,entries){
                if(ts->port==as->port && ts->protocol==as->ipprotocol){
                    LOG_WRITE(0,"Can't Setup service %s at port %s %d with protocol %s version %d\n",
                            as->servicename,p->p_name,(as->port),as->protoname,as->version);
                    struct pm_result pr;
                    pr.length=(sizeof(pr));
                    pr.magic_number=(PM_SETUPNO);
                    pr.port=(as->port);
                    pr.ipprotocol=as->ipprotocol;
                    send(fd,&pr,sizeof(pr),0);
                    goto finish;
                }
            }
            LOG_WRITE(0,"Setup service %s at port %s %d with protocol %s version %d\n",
                    as->servicename,p->p_name,(as->port),as->protoname,as->version);

            int slen=sizeof(struct service)+strlen(as->servicename)+1;
            struct service *s=malloc(slen);
            bzero(s,slen);
            s->port=as->port;
            s->protocol=as->ipprotocol;
            int i=0;
            for(;i<strlen(as->servicename);i++){
                if(as->servicename[i]==' ')
                    as->servicename[i]='_';
            }
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
            struct pm_result pr;
            pr.length=(sizeof(pr));
            pr.magic_number=(PM_SETUPOK);
            pr.port=(as->port);
            pr.ipprotocol=as->ipprotocol;
            send(fd,&pr,sizeof(pr),0);
        }
        if(as->magic_number==PM_RELEASE){
            struct protoent *p=getprotobynumber(as->ipprotocol);
            struct service *ts;
            TAILQ_FOREACH(ts,&pm.services,entries){
                if(ts->port==as->port && ts->protocol==as->ipprotocol){
                    LOG_WRITE(0,"Release service %s at port %s %d with protocol %s version %d\n",
                            ts->servicename,p->p_name,(ts->port),ts->protoname,ts->version);
                    TAILQ_REMOVE(&pm.services,ts,entries);
                    free(ts);
                    break;
                }
            }
        }

finish:
        c->rbuf=NULL;
        c->rlen=0;
        c->offset=0;


    }
}

void handlec(evutil_socket_t fd, short what, void *arg){
    struct client *c=arg;
    /*
    printf("Got an event on client socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
            */

    if(what&EV_TIMEOUT){
        c->tos--;
        if(c->tos<=0){
            event_del(c->e);
            close(fd);
            free(c->e);
            free(c);
            //printf("Idle timeout. Connection finished on fd %d\n",fd);
            return;
        }
    }
    if(what&EV_READ){
        c->tos=2;
        size_t len=read(fd,c->rbuf,c->rlen-c->offset); 
        if(len==0 || len==-1){
            event_del(c->e);
            close(fd);
            free(c->e);
            free(c);
            //printf("Connection finished on fd %d\n",fd);
            return;
        }
        //printf("Read %ld bytes. expected %d\n",len,c->rlen-c->offset);
        c->offset+=len;
        char *fs;
        while((fs=strstr((const char*)c->rbuf,"\r\n"))!=NULL){
            *fs='\0';
            char slen=(long)fs-(long)c->rbuf;
            //printf("Received %s %d\n",c->rbuf,slen);
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

                    //sleep(1); //test
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
    /*
    printf("Got an event on listen unix socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
            */
    int cfd=accept(fd,NULL,NULL);
    struct connection *con=malloc(sizeof(struct connection));
    bzero(con,sizeof(struct connection));
    con->fd=cfd;
    con->e=event_new(pm.loop, cfd, EV_READ|EV_PERSIST, handleu, con);
    event_add(con->e,NULL);
    LOG_WRITE(0,"Service accepted fd %d\n",cfd);
};

void acceptc(evutil_socket_t fd, short what, void *arg){
    /*
    printf("Got an event on listen inet socket %d:%s%s%s%s\n",
            (int) fd,
            (what&EV_TIMEOUT) ? " timeout" : "",
            (what&EV_READ)    ? " read" : "",
            (what&EV_WRITE)   ? " write" : "",
            (what&EV_SIGNAL)  ? " signal" : "");
            */
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
    //printf("Client accepted fd %d\n",cfd);
};

int listenu(){
    pm.fdu = socket(PF_UNIX, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fdu == -1) {  
        pm.fdu=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error socket(): %s",(char*)&ster);
        return -1;  
    }  
    int on = 1; 
    if (setsockopt(pm.fdu, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        pm.fdu=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    unlink(pm.sinu.sun_path);
    if (bind(pm.fdu, (const struct sockaddr *)&pm.sinu, sizeof(struct sockaddr_un)) == -1) {
        pm.fdu=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error bind(): %s",(char*)&ster);
        return -1;
    }
    if (listen(pm.fdu, 64/*backlog*/) == -1) {  
        pm.fdu=-1;
        LOG_WRITE(1,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    return 0;
}

int listen4(){
    pm.fd4 = socket(PF_INET, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fd4 == -1) {  
        pm.fd4=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error socket(): %s",(char*)&ster);
        return -1;  
    }  
    int on = 1; 
    if (setsockopt(pm.fd4, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        pm.fd4=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    if (bind(pm.fd4, (const struct sockaddr *)&pm.sin4, sizeof(struct sockaddr_in)) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        pm.fd4=-1;
        char pbuf[64];
        inet_ntop(AF_INET,&pm.sin4.sin_addr ,pbuf,64);
        LOG_WRITE(1,"Error bind(): %s on %s:%d",(char*)&ster,pbuf,ntohs(pm.sin4.sin_port));  
        return -1;
    }
    if (listen(pm.fd4, 64/*backlog*/) == -1) {  
        pm.fd4=-1;
        LOG_WRITE(1,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    return 0;
}

int listen6(){
    pm.fd6 = socket(PF_INET6, SOCK_STREAM, 0);  
    char ster[128];
    if (pm.fd6 == -1) {  
        pm.fd6=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error socket(): %s",(char*)&ster);
        return -2;  
    }  
    int on = 1; 
    if (setsockopt(pm.fd6, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  
        pm.fd6=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error setsockopt(): %s",(char*)&ster);  
        return -1;  
    } 
    if (setsockopt(pm.fd6, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0){
        pm.fd6=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        LOG_WRITE(1,"Error setsockopt(): %s",(char*)&ster);  
        return -3;
    }
    if (bind(pm.fd6, (struct sockaddr*)&pm.sin6, sizeof(struct sockaddr_in6)) == -1) {
        pm.fd6=-1;
        strerror_r(errno,(char*)&ster,sizeof(ster));
        char pbuf[64];
        inet_ntop(AF_INET6,&pm.sin6.sin6_addr ,pbuf,64);
        LOG_WRITE(1,"Error bind(): %s on %s:%d",(char*)&ster,pbuf,ntohs(pm.sin6.sin6_port));  
        return -1;
    }
    
    if (listen(pm.fd6, 64/*backlog*/) == -1) {  
        pm.fd6=-1;
        LOG_WRITE(1,"Error listen(): %s",strerror(errno));
        return -1;  
    }  
    return 0;
}

int run_portmap(void){
    signal (SIGPIPE,SIG_IGN);
    signal (SIGCHLD,SIG_IGN);
    TAILQ_INIT(&pm.services);
    pm.sin4.sin_family=AF_INET;
    pm.sin4.sin_addr.s_addr=INADDR_ANY;
    pm.sin4.sin_port=htons(1248);
    pm.sin6.sin6_family=AF_INET6;
    pm.sin6.sin6_addr=in6addr_any;
    pm.sin6.sin6_port=htons(1248);
    pm.sinu.sun_family=AF_UNIX;
    sprintf(pm.sinu.sun_path,"/tmp/portmap.1248");
    pm.sinu.sun_len=strlen(pm.sinu.sun_path);
    int runs=0;
    if(listen4()==0){
        runs|=1;
    }else{
        LOG_WRITE(1,"Can't bind IPv4 socket. I'll try to continue");
    }
    if(listen6()==0){
        runs|=2;
    }else{
        LOG_WRITE(1,"Can't bind IPv6 socket. I'll try to continue");
    }
    if(runs==0){
        LOG_WRITE(1,"Can't bind any IP socket. Can't continue");
        return(3);
    }
    if(listenu()!=0){
        LOG_WRITE(1,"Can't bind UNIX socket %s. Can't continue",pm.sinu.sun_path);
        return(2);
    }
    pid_t newpid=fork();
    if(newpid>0)
        return 0;
    if(newpid==-1){
        LOG_WRITE(1,"Can't fork: %s",strerror(errno));
        return(1);
    }
    
    LOG_WRITE(0,"%s started",banner);

    pm.loop = event_base_new();
    pm.e4=event_new(pm.loop, pm.fd4, EV_READ|EV_PERSIST, acceptc, NULL);
    event_add(pm.e4,NULL);
    pm.e6=event_new(pm.loop, pm.fd6, EV_READ|EV_PERSIST, acceptc, NULL);
    event_add(pm.e6,NULL);
    pm.eu=event_new(pm.loop, pm.fdu, EV_READ|EV_PERSIST, acceptu, NULL);
    event_add(pm.eu,NULL);


    int fd = open("/dev/tty", O_RDWR);
    ioctl(fd, TIOCNOTTY, NULL);

    setpgid(getpid(), 0);

    event_base_dispatch(pm.loop);
    return 0;
}
