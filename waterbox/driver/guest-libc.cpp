// Guest-only definitions of C library calls the sandbox does not provide.
// One static link: defining them here shadows musl's.
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <string.h>
#include <sys/utsname.h>

extern "C" {

// Boost.Filesystem asks at start-up which kernel it runs on (to choose
// between system calls it is built without anyway): a fixed answer, the
// same on every host.
int uname(struct utsname *u) {
    if (!u) {
        errno = EFAULT;
        return -1;
    }
    memset(u, 0, sizeof *u);
    strcpy(u->sysname, "Linux");
    strcpy(u->nodename, "vita");
    strcpy(u->release, "5.15.0");
    strcpy(u->version, "#1");
    strcpy(u->machine, "x86_64");
    return 0;
}

} // extern "C"
