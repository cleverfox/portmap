#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
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
    a->length=(len);
    strcpy(a->servicename,argv[1]);
    uint16_t portnum=strtol(argv[2], (char **)NULL, 10);
    a->port=(portnum);
    a->magic_number=(PM_SETUP);
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

    char rb[16];
    struct pm_result *pmr=(void*)rb;
    r=read(fdu,rb,2);
    if(r!=2){
        printf("Error reading response length!\n");
        exit(1);
    }
    printf("to read %d\n",pmr->length);
    if(pmr->length>16){
        printf("error. Portmapper wants answer with too long payload (%d bytes)!\n",pmr->length);
    }
    while((r=read(fdu,rb+2,pmr->length-2))>0){
        if(r!=pmr->length-2){
            printf("Error %d!\n",r);
            exit(1);
        }
        if(pmr->magic_number==PM_SETUPOK){
            printf("Successfully registered port %d with protocol %d\n",pmr->port,pmr->ipprotocol);
            while(read(fdu,a,len)>0);
        }else{
            printf("Can't register port %d at protocol %d\n",pmr->port,pmr->ipprotocol);
            exit(1);
        }
    }
    return 0;
}

