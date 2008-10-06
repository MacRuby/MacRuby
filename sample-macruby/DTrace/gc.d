#!/usr/sbin/dtrace -s

#pragma D option quiet

BEGIN
{
    printf("Target pid: %d\n\n", $target);
    printf("%-10s %-10s %-10s %-10s\n", "OBJECTS", "BYTES", "DURATION", "TYPE");
    printf("--------------------------------------------------------------------------------\n");
}

pid$target::auto_trace_collection_begin:entry
{
    self->starttime = walltimestamp / 1000;
}

pid$target::auto_trace_collection_end:entry
{
    printf("%-10d %-10d %-10d %-10s\n", arg2, arg3, 
	(walltimestamp / 1000) - self->starttime,
	arg1 ? "generational" : "full");
}
