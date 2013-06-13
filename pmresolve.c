#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <time.h>
//#include <event2/event.h>
#include <netinet/in.h>
#include "portmap.h"


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

/*
void doread(evutil_socket_t fd, short what, void *arg){
    pm_sockread(arg);
    int res;
    while(1){
        char *s=pm_readSockLine(arg,&res);
        if(res==-3)
            break;
        if(res<0)
            exit(0);
        printf("%s\n",s);
    }
}
*/


int main(int argc, char *argv[]){
    if(argc<3){
        printf("min 2 args: <host> <service name>\n");
        exit(10);
    }
    /*
    struct event_base *loop;
    loop = event_base_new();
    event_base_dispatch(loop);
    */
#ifdef complex
    int fdu=pm_cconnect(argv[1]);
    if(fdu==-1){
        char ster[128];
        strerror_r(errno,(char*)&ster,sizeof(ster));
        printf("Error socket(): %s\n",(char*)&ster);
        exit(1);
    }
    //pm_setNonblocking(fdu);
    struct portresolver pr;
    pr.fd=fdu;
    pr.block=0;
    pr.offset=0;
    pr.buflen=255;
    pr.start=0;
    pr.timeout=0.01;

    char *s=NULL;
    pr.block=1;
    s=pm_readSockLine(&pr,NULL);

    int r;
    char a[255];
    sprintf(a,"LOOKUP %s\r\n",argv[2]);
    r=send(fdu,a,strlen(a),0);

    int res;
    while(1){
        char *s=pm_readSockLine(&pr,&res);
        if(res==-70){
            printf("timeout\n");
            continue;
        }
        if(res==-3)
            break;
        if(res<0)
            exit(0);
        printf("%s\n",s);
    }
#else
#define use_getaddrinfo
#ifdef use_getaddrinfo
  struct pm_resolve res[10];
  struct addrinfo *ra=NULL,hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  int tr=getaddrinfo(argv[1],"1248",&hints,&ra);
  if(tr==0){
      int r=-1;
      struct addrinfo *res;
      for (res = ra; res; res = res->ai_next) {
          struct pm_resolve rres[10];
          //printf("F1 %d T %d P %d\n",res->ai_family, res->ai_socktype, res->ai_protocol);
          int fdu = socket(res->ai_family, res->ai_socktype, res->ai_protocol);  
          if (fdu < 1) 
              continue;
          //printf("A1 %08x L %d %ld \n", ((struct sockaddr_in*)&res->ai_addr)->sin_addr.s_addr, res->ai_addrlen, sizeof(struct sockaddr_in6));
          if (connect(fdu, res->ai_addr, res->ai_addrlen) == -1)
              continue;
          char buffer[128];
          switch (res->ai_addr->sa_family){
              case AF_INET:
                  inet_ntop(res->ai_addr->sa_family, &((struct sockaddr_in *)res->ai_addr)->sin_addr , buffer, 127);
                  break;
              case AF_INET6:
                  inet_ntop(res->ai_addr->sa_family, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, buffer, 127);
                  break;
          }
          pm_setNonblocking(fdu);
          printf("Connected to %s\n",buffer);
          r=simpleresolve2(fdu,"*",rres,10);
          printf("res %d\n",r);
          if(r){
              
              int i=0;
              for(;i<r;i++){
                  struct pm_resolve *p=&rres[i];
                  printf("%s srv:%s prot:%s ver:%d ipprot:%s port:%d\n",buffer,p->servicename,p->protoname,p->version,p->ipprotocol,p->port);
              }
          }
          if(r>=0)
              break;
      }
      freeaddrinfo(ra);


  }else{
        printf("Can't resolve\n");
    }
#else
    struct pm_resolve res[10];
    int r=simpleresolve(argv[1],argv[2],res,10);
    printf("res %d\n",r);
    if(r){
        int i=0;
        for(;i<r;i++){
            struct pm_resolve *p=&res[i];
            printf("- srv:%s prot:%s ver:%d ipprot:%s port:%d\n",p->servicename,p->protoname,p->version,p->ipprotocol,p->port);
        }
    }
#endif
#endif

    //pr.block=0;
    //printf("Request sent: %d\n",r);

    /*
    struct event *e=event_new(loop, fdu, EV_READ|EV_PERSIST, doread, &pr);
    event_add(e,NULL);
    event_base_dispatch(loop);
    */
    return 0;
}

