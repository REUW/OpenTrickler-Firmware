// Minimal lwIP httpd shim: the simulator never serves HTTP, but the REST
// handler signatures in the firmware headers reference struct fs_file.
#ifndef SIM_LWIP_HTTPD_H_
#define SIM_LWIP_HTTPD_H_
#include <stddef.h>
#include <stdint.h>
#define FS_FILE_FLAGS_HEADER_INCLUDED 0x01
struct fs_file {
    const char *data;
    int len;
    int index;
    int flags;
};
#endif
