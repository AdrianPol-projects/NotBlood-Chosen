// What gl4es found out about the phone's GPU (hardext is filled by initialize_gl4es()), for the
// renderer's choices and the log; see androidbits.cpp.

#include "glx/hardext.h"

int gl4es_info_npot(void)
{
    return hardext.npot;
}

int gl4es_info_highp(void)
{
    return hardext.highp;
}
