#include "sys/sdt.h"

int
main ()
{
    STAP_PROBE(testsuite, uprobe_uaddr_mark);
    return 0;
}
