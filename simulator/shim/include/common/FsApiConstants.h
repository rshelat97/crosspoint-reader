#pragma once
// Simulator shim for SdFat's FsApiConstants: the oflag_t type and open flags
// consumed through HalStorage. Values mirror SdFat's; the sim HalStorage maps
// them onto stdio modes.
#include <cstdint>

typedef uint8_t oflag_t;

#ifndef O_RDONLY
#define O_RDONLY 0x00
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x01
#endif
#ifndef O_RDWR
#define O_RDWR 0x02
#endif
#ifndef O_APPEND
#define O_APPEND 0x08
#endif
#ifndef O_CREAT
#define O_CREAT 0x10
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x20
#endif
#ifndef O_EXCL
#define O_EXCL 0x40
#endif
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif
#ifndef O_READ
#define O_READ O_RDONLY
#endif
