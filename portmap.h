#ifndef PORTMAP_H
#define PORTMAP_H
#include <stdint.h>
#include <sys/types.h>

enum mnum {
    PM_SETUP = 0x3c30,
    PM_SETUPOK = 0x6c30,
    PM_SETUPNO = 0x6c32,
    PM_RELEASE = 0x3c31
};
struct assign {
   uint16_t length;
   uint16_t magic_number; 
   uint16_t port;
   uint8_t  ipprotocol;
   uint8_t  version;
   char protoname[16];
   char servicename[];
};
struct pm_result {
   uint16_t length;
   uint16_t magic_number; 
   uint16_t port;
   uint8_t  ipprotocol;
};

struct portresolver {
    int fd;
    int block;
    int start;
    int offset;
    int buflen;
    char buf[256];
    double timeout;
};
struct pm_resolve {
    uint16_t port;
    uint8_t  version;
    char ipprotocol[16];
    char protoname[16];
    char servicename[32];
};

int pm_sockread(struct portresolver *pr);
char *pm_readSockLine(struct portresolver *pr, int *res);
int pm_setNonblocking(unsigned int sd);
int pm_cconnect(char *address);
int pm_register(int fd, char* servicename, char* protocol, unsigned short port, char* protocolname, unsigned char protocolversion);
int pm_unregister(int fd, char* protocol, unsigned short port); 
int simpleresolve(char *host, char *service, struct pm_resolve *pms, int pmsc);
int simpleresolve2(int fdu, char *service, struct pm_resolve *pms, int pmsc);

#endif
