#!/usr/sbin/dtrace -s

/* This script should be run against MacRuby 64-bit */

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
}

objc$target::-finalize:entry
{
    isaptr = *(uint64_t *)copyin(arg0, 8);
    class_rw_t = *(uint64_t *)copyin(isaptr + (4 * 8), 8);
    class_ro_t = *(uint64_t *)copyin(class_rw_t + (2 * 4), 8);
    classnameptr = *(uint64_t *)copyin(class_ro_t + (4 * 4) + 8, 8);
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
