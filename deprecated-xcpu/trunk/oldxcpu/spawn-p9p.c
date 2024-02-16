/* 
 * spawn the same binary for this client over a list of nodes
 */
#include "xcpusrv.h"

CFsys*
xparse(char *name)
{
	int fd;
	CFsys *fs;

	if((fd = dial(addr, nil, nil, nil)) < 0)
		sysfatal("dial: %r");
	if((fs = fsmount(fd, nil)) == nil)
		sysfatal("ERROR: fsamount: %r");

	return fs;
}

/* 
 * note: addr should be comma-separated list of dial strings 
 */
int
spawn(Client *c, char *head, char *tail)
{
	CFid *fid;
	CFsys *fs;
	char *err = nil;

	fs = xparse(head);
	if(fs == nil)
		return 0;

	fid = fsopen(fs, "/clone", OREAD);
	if(fid == nil)

	writestring(name, "ctl", "type persistent");	
	fsclose(fid);

Bad1:
	free(fs);
	wrerrstr(err);
	return 0;
}

