#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netdb.h>
#include "portmap.h"

int main(int argc, char *argv[]){
    if(argc<3){
        printf("min 2 args\n");
        exit(10);
    }
    struct sockaddr_un sun;
    sun.sun_family=AF_UNIX;
    sprintf(sun.sun_path,"/tmp/portmap.1248");
    sun.sun_len=strlen(sun.sun_path);

    int fdu = socket(PF_UNIX, SOCK_STREAM, 0);  
    char ster[128];
    if (fdu == -1) {  
        strerror_r(errno,(char*)&ster,sizeof(ster));
        printf("Error socket(): %s\n",(char*)&ster);
        return -1;  
    }  
    if (connect(fdu, (const struct sockaddr *)&sun, sizeof(struct sockaddr_un)) == -1) {
        strerror_r(errno,(char*)&ster,sizeof(ster));
        printf("Error connect(): %s\n",(char*)&ster);
        return -1;
    }
    int len=sizeof(struct assign)+strlen(argv[1])+1;
    struct assign *a=alloca(len);
    bzero(a,len);
    a->length=htons(len);
    strcpy(a->servicename,argv[1]);
    uint16_t portnum=strtol(argv[2], (char **)NULL, 10);
    a->port=htons(portnum);
    a->magic_number=htons(PM_SETUP);
    struct protoent * p=getprotobyname("tcp");
    if(argc>=4){
       p=getprotobyname(argv[3]);
    }
    if(argc>=5){
       strncpy(a->protoname,argv[4],15);
    }
    if(argc>=6){
        a->version=atoi(argv[5]);
    }
    a->ipprotocol=p->p_proto;
    int r=send(fdu,a,len,0);
    printf("Request sent: %d\n",r);
    while(read(fdu,a,len)>0);
    return 0;
}

