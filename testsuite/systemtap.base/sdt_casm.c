#include <sys/sdt.h>

int main()
{
    int x = 42;
    __asm__ __volatile__ (
            STAP_PROBE_ASM(testsuite, probe0, STAP_PROBE_ASM_TEMPLATE(0))
            );
    __asm__ __volatile__ (
            STAP_PROBE_ASM(testsuite, probe1, STAP_PROBE_ASM_TEMPLATE(1))
            :: STAP_PROBE_ASM_OPERANDS(1, x)
            );
    return 0;
}
