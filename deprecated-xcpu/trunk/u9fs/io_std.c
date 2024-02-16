#include <stdio.h>

long
u9fs_write(int fd, void *buf, long count) 
{
	return write(fd, buf, count);
}

long 
u9fs_read(int fd, void *buf, long count)
{
	return read(fd, buf, count);
}	
