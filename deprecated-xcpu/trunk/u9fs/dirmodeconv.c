#include <plan9.h>
#include <fcall.h>

static char *modes[] =
{
	"---",
	"--x",
	"-w-",
	"-wx",
	"r--",
	"r-x",
	"rw-",
	"rwx",
};

static void
rwx(long m, char *s)
{
	strncpy(s, modes[m], 3);
}

int
dirmodeconv(va_list *arg, Fconv *f)
{
	static char buf[16];
	ulong m;

	m = va_arg(*arg, ulong);

	if(m & DMDIR)
		buf[0]='d';
	else if(m & DMAPPEND)
		buf[0]='a';
	else if(m & DMTMP)
		buf[0]='t';
	else if(m & DMSYMLINK)
		buf[0]='l';
	else if(m & DMDEVICE)
		buf[0]='b';
	else if(m & DMNAMEDPIPE)
		buf[0]='p';
	else if(m & DMSOCKET)
		buf[0]='s';
	else 
		buf[0]='-';
	if(m & DMEXCL)
		buf[1]='L';
	else
		buf[1]='-';
	rwx((m>>6)&7, buf+2);
	rwx((m>>3)&7, buf+5);
	rwx((m>>0)&7, buf+8);

	if(m & DMSETUID) 
		*(buf+4) = 'S';
	if(m & DMSETGID)
		*(buf+7) = 'S';

	buf[11] = 0;

	strconv(buf, f);
	return 0;
}
