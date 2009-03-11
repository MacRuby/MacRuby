#!/usr/sbin/dtrace -s

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
    printf("%20s %-5s %10s %-30s %10s\n", "FILE", "LINE", "CLASS", "METHOD",
	    "DURATION");
    printf("--------------------------------------------------------------------------------\n");
}

macruby$target:::method-entry
{
    self->starttime = walltimestamp / 1000;
}

macruby$target:::method-return
{
    printf("%20s:%-5d %10s#%-30s %10d\n", copyinstr(arg2), arg3,
            copyinstr(arg0), copyinstr(arg1),
	    (walltimestamp / 1000) - self->starttime);
}
