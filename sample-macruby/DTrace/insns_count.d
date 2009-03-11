#!/usr/sbin/dtrace -s

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
}

macruby$target:::insn-entry
{
    @[copyinstr(arg0)] = count();
}

END
{
    printf("\n");
    printf("%30s       %-30s\n", "INSN", "COUNT");
    printf("--------------------------------------------------------------------------------\n");
    printa("%30s       %-30@d\n", @);
}
