#ifndef PORTMAP_H
#define PORTMAP_H
struct assign {
   uint16_t length;
   uint16_t magic_number; 
   uint16_t port;
   uint16_t protocol;
   uint8_t  version;
   char protoname[16];
   char servicename[];
};

#endif
