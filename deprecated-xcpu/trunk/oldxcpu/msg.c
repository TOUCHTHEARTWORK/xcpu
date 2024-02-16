/* again, this code is from Plan 9 and has the Lucent Copyrights
  * with mods for xcpu
  */
#include "xcpusrv.h"

char *msgnames[] =
{
	"XCPU_MSG_STDOUT",
	"XCPU_MSG_STDERR",
	"XCPU_MSG_STDOUT_EOF",
	"XCPU_MSG_STDERR_EOF"
};

void
badmsg(Msg *m, int want)
{
	char *s, buf[20+ERRMAX];

	if(m==nil){
		snprint(buf, sizeof buf, "<early eof: %r>");
		s = buf;
	}else{
		snprint(buf, sizeof buf, "<unknown type %d>", m->type);
		s = buf;
		if(m->type > 0 && m->type != 0 && m->type < nelem(msgnames))
			s = msgnames[m->type];
	}
	if(want)
		error("got %s message expecting %s", s, msgnames[want]);
	error("got unexpected %s message", s);
}

void
msgdump(Msg *m, char *info){
	int num  = -1;
	if (m->c)
		num = m->c->num;

	debug(DBG_FS, "C%d:msgdump: %s %p: ", num, info, m);
	debug(DBG_FS, "C%d:msgdump: type %d len %d bp %p rp %p wp %p ep %p\n",
		num,
		m->type,
		m->len,		
		m->bp,	
		m->rp,		
		m->wp,	
		m->ep);	
}

Msg*
allocmsg(Client *c, int type, int len)
{
	uchar *p;
	Msg *m;

	if(len > 256*1024)
		abort();

	m = (Msg*)emalloc9p(sizeof(Msg)+4+8+1+len+4);
	setmalloctag(m, getcallerpc(&c));
	p = (uchar*)&m[1];
	m->c = c;
	m->bp = p;
	m->rp = m->bp;
	m->ep = p+len;
	m->wp = p;
	m->type = type;
	msgdump(m, "allocmsg");
	return m;
}

uchar
getbyte(Msg *m)
{
	if(m->rp >= m->ep)
		error(Edecode);
	return *m->rp++;
}

ushort
getshort(Msg *m)
{
	ushort x;

	if(m->rp+2 > m->ep)
		error(Edecode);

	x = SHORT(m->rp);
	m->rp += 2;
	return x;
}

ulong
getlong(Msg *m)
{
	ulong x;

	if(m->rp+4 > m->ep)
		error(Edecode);

	x = LONG(m->rp);
	m->rp += 4;
	return x;
}

char*
getstring(Msg *m)
{
	char *p;
	ulong len;

	/* overwrites length to make room for NUL */
	len = getlong(m);
	if(m->rp+len > m->ep)
		error(Edecode);
	p = (char*)m->rp-1;
	memmove(p, m->rp, len);
	p[len] = '\0';
	return p;
}

void*
getbytes(Msg *m, int n)
{
	uchar *p;

	if(m->rp+n > m->ep)
		error(Edecode);
	p = m->rp;
	m->rp += n;
	return p;
}

void
putbyte(Msg *m, uchar x)
{
	if(m->wp >= m->ep)
		error(Eencode);
	*m->wp++ = x;
}

void
putshort(Msg *m, ushort x)
{
	if(m->wp+2 > m->ep)
		error(Eencode);
	PSHORT(m->wp, x);
	m->wp += 2;
}

void
putlong(Msg *m, ulong x)
{
	if(m->wp+4 > m->ep)
		error(Eencode);
	PLONG(m->wp, x);
	m->wp += 4;
}

void
putstring(Msg *m, char *s)
{
	int len;

	len = strlen(s);
	putlong(m, len);
	putbytes(m, s, len);
}

void
putbytes(Msg *m, void *a, long n)
{
	if(m->wp+n > m->ep)
		error(Eencode);
	memmove(m->wp, a, n);
	m->wp += n;
}
