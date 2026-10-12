#include "ggponet.h"
#include <poll.h>
#include <zlib.h>

/* Consumers must see the system headers, not GGPO's private poll.h/zlib.h. */
#ifndef ZLIB_VERNUM
#error "GGPO must not expose its bundled legacy zlib.h to consumers"
#endif

int main(void) {
    if (poll((struct pollfd*)0, 0, 0) != 0) return 1;
    return ggpo_close_session((GGPOSession*)0) == GGPO_ERRORCODE_INVALID_SESSION ? 0 : 1;
}
