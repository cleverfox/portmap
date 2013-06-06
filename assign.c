#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <stdlib.h>
#include "portmap.h"

int main(int argc, char *argv[]){
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
    strcpy(a->servicename,argv[0]);
    int r=send(fdu,a,len,0);
    printf("Request sent: %d\n",r);
    sleep(90);
    return 0;
}

