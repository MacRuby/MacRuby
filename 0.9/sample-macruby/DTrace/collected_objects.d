#!/usr/sbin/dtrace -s

/* This script should be run against MacRuby 32-bit */

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
}

objc$target::-finalize:entry
{
    isaptr = *(uint32_t *)copyin(arg0, 4);
    classnameptr = *(uint32_t *)copyin(isaptr + 8, 4);
    classname = copyinstr(classnameptr);

    @[classname] = count();
}

END
{
    printf("\n");
    printf("%50s       %-10s\n", "CLASS", "COUNT");
    printf("--------------------------------------------------------------------------------\n");
    printa("%50s       %-10@d\n", @);
}
