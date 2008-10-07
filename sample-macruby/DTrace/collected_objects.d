#!/usr/sbin/dtrace -s

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
}

objc$target::-finalize:entry
{
    /* Thanks http://www.friday.com/bbum/2008/01/26/objective-c-printing-class-name-from-dtrace/ 
     * TODO does not work in 64-bit
     */
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
