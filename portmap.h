#ifndef PORTMAP_H
#define PORTMAP_H
enum mnum {
    PM_SETUP = 0x3c30
};
struct assign {
   uint16_t length;
   enum mnum magic_number; 
   uint16_t port;
   uint16_t protocol;
   uint8_t  version;
   char protoname[16];
   char servicename[];
};

#endif
