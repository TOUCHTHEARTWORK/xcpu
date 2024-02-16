#include "xcpusrv.h"

typedef struct Tab Tab;
struct Tab
{
	char *name;
	ulong mode;
};

int debuglevel = 0/*xff*/;
int doabort = 0;

/* default userid and names for the server */
char *defuid = "xcpusrv";
char *defgid = "xcpusrv";
char *defmuid = "xcpusrv";
uint defnuid = 99;  /* nobody */
uint defngid = 0;  /* root */
/* uid/gid, etc for clone and sessions */
char *uid, *gid, *muid;

/* host we will register to */
char *reghost;

int nuid, ngid;
ulong mtime, mode;

extern char *procnames[];
extern char *proctok[];
extern int procstr[];
extern int proclast;

void
usage(void)
{
	print("usage: %s [-A authlist] [-c cipherlist] [-m mtpt] [-u uid] [-g gid] [-U nuid] [-G ngid] [-r host] [user@]hostname\n", argv0);
	threadexits("usage");
}

enum
{
	Qroot,
	Qclone,
	Qprocs,
	Quname,
	Qarch,
	Qaddr,
	Qn,
	Qexec,
	Qargv,
	Qctl,
	Qstatus,
	Qstdin,
	Qstdout,
	Qstderr,
	Qlastpid,
	Qwait,

	Stack = 8192*2,
	IoSize = 80,
};

Tab tab[] =
{
	"/",		DMDIR|0555,
	"clone",	0444,
	"procs",	0444,
	"uname", 	0444,
	"arch", 	0444,
	"addr", 	0444,
	nil,		DMDIR|0555,
	"exec", 	0666,
	"argv", 	0666,
	"ctl",		0666,
	"status",	0444,
	"stdin", 	0666,
	"stdout", 	0444,
	"stderr", 	0444,
	"lastpid", 	0444,
	"wait", 	0444,
};


/* due to stupidity in linux, we are going to a format with a generation in the top 8 bits. */

#define PATH(type, n, generation)      	((type)|((n)<<8)|(generation<<24))
#define TYPE(path)			((int)(path) & 0xFF)
#define NUM(path)			((uint)(path)>>8)
#define PATHINDEX(path)			((uint)((path)>>8)&0xffff)

Channel *xcpumsgchan;		/* chan(Msg*) */
Channel *fsreqchan;			/* chan(Req*) */
Channel *fsreqwaitchan;		/* chan(nil) */
Channel *fsclunkchan;		/* chan(Fid*) */
Channel *fsclunkwaitchan;	/* chan(nil) */
ulong time0;

enum
{
	Closed,
	Setup,
	Established,
	Teardown
};

char *statestr[] = {
	"Closed",
	"Setup",
	"Established",
	"Teardown",
};


int nclient = 0;
int serial = 0;
Client **client = 0;

/* yeah, you can do this with fmtinstall. we're running out of letters. */

void
fcalldump(Fcall *f, char *info) {
	debug(DBG_FS, "fcalldump: %s: ", info);
	debug(DBG_FS, "fcalldump: type 0x%x, fid 0x%ulx, tag 0x%x, count 0x%ulx, data %p\n", f->type, f->fid, f->tag, f->count, f->data);
}

void
reqdump(Req *r, char *info){
	debug(DBG_FS, "reqdump: %s %p: ", info, r);
	debug(DBG_FS, "reqdump: tag 0x%lx, aux %p, fid %p, afid %p, newfid %p\n", r->tag, r->aux, r->fid, r->afid, r->newfid);
	fcalldump(&r->ifcall, "   ifcall is: ");
	fcalldump(&r->ofcall, "  ofcall is: ");
}

int
newclient(void)
{
	int i;
	Client *c = nil;
//	debug(DBG_CLIENT, "newclient: nclient=%d, client=%p\n", nclient, client);
	for(i = 0; (i < nclient) && client; i++){
//		debug(DBG_CLIENT, "newclient: client=%p, ref=%d\n", client[i], client[i]->ref);
		if(client[i] && 
			client[i]->ref == 0 && 
			client[i]->state == Closed){
				int gen;
				int num;
				
		  		c = client[i];
				gen = c->gen;
				gen++;
				num = c->num;
				memset(c, 0, sizeof(*c));
				c->gen = gen;
				c->type = Normal;
				c->num = num;
				c->state = Setup;
				c->lastpid = -1;
				if(c->uid)
					free(c->uid);
				c->uid = estrdup9p(uid);
				if(c->gid)
					free(c->gid);
				c->gid = estrdup9p(gid);
				if(c->muid)
					free(c->muid);
				c->muid = estrdup9p(muid);
				c->nuid = nuid;
				c->ngid = ngid;
				c->mode = mode;
				c->kiddead = 0;
				/* if s_new() fails we'll boldly try to continue */
				s_reset(c->waitstr);
				debug(DBG_CLIENT, "C%d:newclient:ALLOCATE OLD %d\n", i,i);
				return i;
		}
	}

	if(nclient%16 == 0)
		client = erealloc9p(client, (nclient+16)*sizeof(client[0]));

	c = emalloc9p(sizeof(Client));
	memset(c, 0, sizeof(*c));
	c->gen = 1;
	c->type = Normal;
	c->state = Setup;
	c->num = nclient;
	c->argv = nil;
	
	/* the files for this client will have uid/gid pairs identical
	 * to the ones 'clone' had at the time it was opened
	 */
	c->uid = estrdup9p(uid);
	c->gid = estrdup9p(gid);
	c->muid = estrdup9p(muid);
	c->nuid = nuid;
	c->ngid = ngid;
	c->mode = mode;
	c->kiddead = 0;
	c->waitstr = s_new();
	
	/* test! */
	//c->ref++;

	client[nclient++] = c;
	debug(DBG_CLIENT, "C%d:newclient:ALLOCATE client=%p\n", c->num,c);
	return c->num;
}

void
queuereq(Client *c, Req *r)
{
	debug(DBG_IO, "C%d:queuereq: %p to client %p\n", c->num, r, c);
	if(c->rq==nil)
		c->erq = &c->rq;
	*c->erq = r;
	r->aux = nil;
	c->erq = (Req**)&r->aux;
}

void
queuemsg(Client *c, int which, Msg *m)
{
	if(c->mq[which]==nil)
		c->emq[which] = &c->mq[which];
	*c->emq[which] = m;
	m->link = nil;
	c->emq[which] = (Msg**)&m->link;
}

void
matchreqmsg(Client *c, int which, Req *r) {
	Msg *m;
	int n, rm;
	debug(DBG_IO, "C%d:matchreqmsg: r %p c %p\n", c->num, r, c);
	rm = 0;
	m = c->mq[which];
	debug(DBG_IO, "C%d:matchreqmsg: m %p\n", c->num, m);
	if (! m){
		respond(r, nil);
		return;
	}

	n = r->ifcall.count;

	if (m->type >= XCPU_MSG_STDOUT_EOF) {
		c->mqeof[which] = 1;
		c->kideof++;
	}

	if(n >= m->ep - m->rp){
		n = m->ep - m->rp;
		c->mq[which] = m->link;
		rm = 1;
	}
	debug(DBG_IO, "C%d:matchreqmsg: move %d from %p to %p\n", c->num, n, 
				m->rp, r->ofcall.data);
	memmove(r->ofcall.data, m->rp, n);
	if(rm)
;	//	free(m);
	else
		m->rp += n;
	r->ofcall.count = n;

	debug(DBG_IO, "C%d:matchreqmsg: respond with %d bytes to a read, responded %d\n",
				c->num, n, r->responded);
	respond(r, nil);
}

void
matchmsgs(Client *c, int which)
{
	Req *r;

	debug(DBG_IO, "C%d:matchmsgs\n",c->num);
	while(c->rq && c->mq[which]){
		debug(DBG_IO, "C%d:matchmsgs: type=%d\n", c->num,which);
		r = c->rq;
		reqdump(r, "matchmsg");
		c->rq = r->aux;

		matchreqmsg(c, which, r);

	}
	/* did we hit eof on the mq? */
	while (c->rq && (! c->mq[which]) && c->mqeof[which]) {
		debug(DBG_IO, "C%d:matchreqmsg: %d fast path, %d\n", 
			c->num, which, c->mqeof[which]);
		r = c->rq;
		c->rq = r->aux;
		respond(r, nil);
	}
}

Req*
findreq(Client *c, Req *r)
{
	Req **l;

	for(l=&c->rq; *l; l=(Req**)&(*l)->aux){
		if(*l == r){
			*l = r->aux;
			if(*l == nil)
				c->erq = l;
			return r;
		}
	}
	return nil;
}

static void
stdineof(Client *c){
	debug(DBG_CLIENT, "C%d:stdineof: Closing stdin fd %d\n", c->num,c->stdinfd[1]);
	(void) close(c->stdinfd[0]);
	(void) close(c->stdinfd[1]);
	/* kick off any pending reads that might get eof */
	matchmsgs(c, 0);
	matchmsgs(c, 1);
//	c->mqeof[0] = 1;
}


void
teardownclient(Client *c)
{
	Msg *m = 0;
	USED(c);
	USED(m);
	debug(DBG_CLIENT, "C%d:stdineof: tear down client, gen %d\n", c->num, c->gen);
	c->state = Teardown;
	/* close leaking fids */
	/* for now, no waiting. */
	if(c->tmpfd > 0) {
		remove(c->tmpname);
		close(c->tmpfd);
	}	

	/* those should never be null, we'd better take a hit here
	 * than try to recover
	 */
	free(c->uid);
	free(c->gid);
	free(c->muid);
//	if(c->waitstr)
//		s_free(c->waitstr);
	debug(DBG_CLIENT, "C%d:TEARDOWN\n", c-> num);
	c->state = Closed;

}
void
closeclient(Client *c, int path)
{
	/* do any specific stuff you need to do to clunk a single Q* */
	switch(path) {
		case Qexec:
			close(c->tmpfd);
			break;
		case Qstdin:
			stdineof(c);
			break;
	}
	if(c->ref > 0)
		c->ref--;
	debug(DBG_CLIENT, "C%d:REF=%d closeclient %s \n", c->num, c->ref, tab[path].name);
	if(c->ref)
		return;

	if(c->type == Persistent)
		return;

	if(c->rq != nil)
		sysfatal("ref count reached zero with requests pending (BUG)");

	if(c->state != Closed)
		teardownclient(c);
}


static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;
	Client *c = nil;

	debug(DBG_FS, "fillstat: d %p, path 0x%llx, type %d, num %d \n", 
				  d, path, TYPE(path), PATHINDEX(path));

	memset(d, 0, sizeof(*d));

	d->uid = estrdup9p(defuid);
	d->gid = estrdup9p(defgid);
	d->muid = estrdup9p(defmuid);
	if(fs.dotu) {
		d->uidnum = defnuid;
		d->gidnum = defngid;
		d->ext = estrdup9p("");
	}

	d->qid.path = path;
	d->atime = d->mtime = time0;
	debug(DBG_FS, "fillstat: TYPE(path) 0x%x\n", TYPE(path));
	t = &tab[TYPE(path)];
	if(t->name)
		d->name = estrdup9p(t->name);
	else{
		d->name = smprint("%ux", NUM(path));
		if(d->name == nil)
			sysfatal("out of memory");
	}
	d->qid.type = t->mode>>24;
	d->mode = t->mode;
	switch(TYPE(path)){
	case Qargv:
		c = client[PATHINDEX(path)];
		debug(DBG_FS, "C%d:fillstat: c %p argv is %p \n", c->num, c, c->argv);
		if (c->argv)
			d->length = strlen(c->argv);
		break;
	case Qn:
		c = client[PATHINDEX(path)];
		d->uid = estrdup9p(c->uid);
		d->gid = estrdup9p(c->gid);
		d->muid = estrdup9p(c->muid);
		if(fs.dotu) {
			d->uidnum = c->nuid;
			d->gidnum = c->ngid;
		}
		break;
	case Qclone:
		d->uid = estrdup9p(uid);
		d->gid = estrdup9p(gid);
		d->muid = estrdup9p(muid);
		if(fs.dotu) {
			d->uidnum = nuid;
			d->gidnum = ngid;
		}
		d->mtime = mtime;
		d->mode = mode;
		break;
	}
	debug(DBG_FS, "fillstat outgoing: %D\n", d);
}

static int
wfillstat(Dir *d, uvlong path)
{
	Client *c = nil;

	debug(DBG_FS, "wfillstat: d %D, path 0x%llx, type %d, num %d\n", 
			d, path, TYPE(path), NUM(path));

	switch(TYPE(path)){
	case Qargv:
		/* just support ftruncate case */
		if (d->length == 0) {
			debug(DBG_FS, "wfillstat: new len 0x%lld\n", 
					d->length);
			c = client[PATHINDEX(path)];
			debug(DBG_FS, "C%d:wfillstat: c %p argv is %p \n", c->num, c, c->argv);
			if (c->argv){
				free(c->argv);
				c->argv = nil;
			}
		}
		break;
	case Qclone:
		debug(DBG_FS, "wfillstat: user: %s, group: %s, nuid: %d ngid: %d, \n", d->uid, d->gid, d->uidnum, d->gidnum);
		if(d->uid[0] != '\0') {
			free(uid);
			uid = estrdup9p(d->uid);
		}
		if(d->gid[0] != '\0') {
			free(gid);
			gid = estrdup9p(d->gid);
		}
		if(d->muid[0] != '\0') {
			free(muid);
			muid = estrdup9p(d->muid);
		}
		if(fs.dotu) {
			nuid = d->uidnum;
			ngid = d->gidnum;
		}
		if(d->mode != (~0))
			mode = d->mode;

		/* do we need this? */
		mtime = time(0);
		break;
	case Qexec:
	case Qctl:
	case Qstdin:
		{ 
			int bad = 0;
		debug(DBG_FS, "wfillstat: user: %s, group: %s, nuid: %d ngid: %d, \n", d->uid, d->gid, d->uidnum, d->gidnum);
		if(d->uid[0] != '\0') {
			debug(DBG_FS, "willstat: can't set uid on Qexec/Qctl/Qstdin\n");
			bad++;
		}
		if(d->gid[0] != '\0') {
			debug(DBG_FS, "willstat: can't set gid on Qexec/Qctl/Qstdin\n");
			bad++;
		}
		if(d->muid[0] != '\0') {
			debug(DBG_FS, "willstat: can't set muid on Qexec/Qctl/Qstdin\n");
			bad++;
		}
		if(fs.dotu) {
			debug(DBG_FS, "willstat: can't set dotu on Qexec/Qctl/Qstdin\n");
			/* ignore for now. */
			//bad++;
		}
		if(d->mode != (~0)){
			debug(DBG_FS, "willstat: can't set mode on Qexec/Qctl/Qstdin\n");
			bad++;
		}

		debug(DBG_FS, "wfillstat: bad is %d on Qctl/Qexec, if > 0 fails!\n", bad);
		if (bad)
			return -1;
		/* all else is harmless, but ignored */
		/* do we need this? */
		mtime = time(0);
	}
		break;
	default:
		return -1;
	}
	return 0;
}

static void
xattach(Req *r)
{
	debug(DBG_FS, "xattach\n");
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	r->fid->qid.path = PATH(Qroot, 0, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
xstat(Req *r)
{
	debug(DBG_FS, "xstat\n");
	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static void
xwstat(Req *r)
{
	debug(DBG_FS, "xwstat\n");

	if(wfillstat(&r->d, r->fid->qid.path) < 0) {
		respond(r, "not allowed to wstat file");
		return;
	}
	
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void*v)
{
	USED(v);
	USED(i);
	i += Qroot+1;
	debug(DBG_FS, "rootgen: i %d\n", i);
	if(i < Qn){
		fillstat(d, i);
		return 0;
	}
	i -= Qn;
	if((i < nclient) && (client[i]->ref || (client[i]->type == Persistent))){
		fillstat(d, PATH(Qn, client[i]->num, client[i]->gen));
		return 0;
	}

	return -1;
}


static int
clientgen(int i, Dir *d, void *aux)
{
	Client *c;
	debug(DBG_FS, "clientgen: i %d, d %p, aux %p\n", i, d, aux);
	c = aux;
	/* handle case where linux is too cache-happy */
	i += Qn+1;
	debug(DBG_FS, "clientgen: i %d, c->num 0x%x, Qlastpid %d\n", i, c->num, Qlastpid);
	if(i <= Qlastpid){
		fillstat(d, PATH(i, c->num, 0));
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i, n;
	char buf[32];
	ulong path;

	debug(DBG_FS, "fswalk1: %s\n", name);
	path = fid->qid.path;
	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0){
		switch(TYPE(path)){
		case Qn:
			qid->path = PATH(Qroot, NUM(path), 0);
			qid->type = tab[Qroot].mode>>24;
			return nil;
		case Qroot:
			return nil;
		default:
			return "bug in fswalk1";
		}
	}

	i = TYPE(path)+1;

	for(; i<nelem(tab); i++){
		if(i==Qn){
		  n = strtol(name, 0, 16);
			snprint(buf, sizeof buf, "%ux", n);
			debug(DBG_FS, "fswalk1: n 0x%x path 0x%x buf %s nclient %d\n", 
			       n, n&0xffff, buf, nclient);
			if ((n&0xffff) < nclient && strcmp(buf, name) == 0){
				qid->path = PATH(i, n&0xffff, client[n&0xffff]->gen);
				qid->type = tab[i].mode>>24;
				return nil;
			}
			break;
		}

		if(strcmp(name, tab[i].name) == 0){

			qid->path = PATH(i, NUM(path), 0);
			qid->type = tab[i].mode>>24;
			return nil;
		}
/* this ain't right *
		if(tab[i].mode&DMDIR)
			break;
	 */
	}
	return "No such file or directory"; //"directory entry not found";
}


static void
ctlread(Req *r, Client *c)
{
	char buf[32];

	/* note what we're doing here! num is unshifted, and 
	 * c-> gen << 16, so the effect is >>8 
	 */
	sprint(buf, "%ux", c->num | (c->gen<<16));
	readstr(r, buf);
	respond(r, nil);
}

/* fix this up later so it is more generic. I just want to see if it works at all */
void
newreadstdout(void *client) {
	struct Client *c = client;
	Ioproc *iop = nil;

	/* also, we of course block on the main process as we read each bit
	  * this will in turn block the child so that we don't get gobs of data
	  * floating around. 
	  */
	int amt = 0;
	char *data;
	Msg *m;

	/* I really miss waserror() */
	iop = ioproc();
	if (! iop) {
		fprint(2, "C%d: ioproc fails: %r\n", c->num);
		goto bad;
	}

	data = emalloc9p(IoSize);
	if (! data) {
		fprint(2, "C%d:readstdout: could not allocate %d bytes for I/O\n", c->num, IoSize);
		goto bad;
	}
	threadsetname("readstdout");
	while ((amt = ioread(iop, c->stdoutfd[0], data, IoSize/*8192*/)) > 0) {
		debug(DBG_CLIENT, "C%d:newreadstdout: read %d bytes ... :%s:\n", c->num, amt, data);
		m = allocmsg(c, XCPU_MSG_STDOUT, amt);
		if (! m) {
			debug(DBG_CLIENT, "C%d:readstdout: alloc of m failed\n", c->num);
			/* what else to do ? continue */
			continue;
		}
		putbytes(m, data, amt);
		m->c = c;
		msgdump(m, "newreadstdout before send");
		sendp(xcpumsgchan, m);
		debug(DBG_CLIENT, "C%d:readstdout: read %d bytes...\n", c->num, amt);
		memset(data, 0, IoSize);
		/* is it dead yet? we can't get EOF from the ioproc */
		if (c->kiddead)
			break;
	}
	free(data);
bad:
	if (iop)
		closeioproc(iop);
	/* send an eof message */
	m = allocmsg(c, XCPU_MSG_STDOUT_EOF, 0);
	if (! m) {
		debug(DBG_CLIENT, "C%d:readstdout: alloc of m failed for eof\n", c->num);
		/* what else to do ? continue */
	} else {
		debug(DBG_CLIENT, "C%d:readstdout: send eof\n", c->num);
		m->c = c;
		sendp(xcpumsgchan, m);
		debug(DBG_CLIENT, "C%d:readstdout: DID SEND an  eof\n", c->num);
	}
	close(c->stdoutfd[0]);
	close(c->stdoutfd[1]);
	threadexits(0);
}

/* fix this up later so it is more generic. I just want to see if it works at all */
void
newreadstderr(void *client) {
	struct Client *c = client;
	Ioproc *iop = nil;

	/* also, we of course block on the main process as we read each bit
	  * this will in turn block the child so that we don't get gobs of data
	  * floating around. 
	  */
	int amt = 0;
	char *data;
	Msg *m;

	/* I really miss waserror() */
	iop = ioproc();
	if (! iop) {
		fprint(2, "C%d: ioproc fails: %r\n", c->num);
		goto bad;
	}

	data = emalloc9p(IoSize);
	if (! data) {
		fprint(2, "C%d:readstderr: could not allocate %d bytes for I/O\n", c->num, IoSize);
		goto bad;
	}
	threadsetname("readstderr");
	while ((amt = ioread(iop, c->stderrfd[0], data, IoSize/*8192*/)) > 0) {
		debug(DBG_CLIENT, "C%d:newreadstderr: read %d bytes ... :%s:\n", c->num, amt, data);
		m = allocmsg(c, XCPU_MSG_STDERR, amt);
		if (! m) {
			debug(DBG_CLIENT, "C%d:readstderr: alloc of m failed\n", c->num);
			/* what else to do ? continue */
			continue;
		}
		putbytes(m, data, amt);
		m->c = c;
		sendp(xcpumsgchan, m);
		debug(DBG_CLIENT, "C%d:readstderr: read %d bytes...\n", c->num, amt);
		memset(data, 0, IoSize);
		/* is it dead yet? we can't get EOF from the ioproc */
		if (c->kiddead)
			break;
	}
	free(data);
bad:
	if (iop)
		closeioproc(iop);
	/* send an eof message */
	m = allocmsg(c, XCPU_MSG_STDERR_EOF, 0);
	if (! m) {
		debug(DBG_CLIENT, "C%d:readstderr: alloc of m failed for eof\n", c->num);
		/* what else to do ? continue */
	} else {
		debug(DBG_CLIENT, "C%d:readstderr: send eof\n", c->num);
		m->c = c;
		sendp(xcpumsgchan, m);
		debug(DBG_CLIENT, "C%d:readstderr: DID SEND an  eof\n", c->num);
	}
	close(c->stderrfd[0]);
	close(c->stderrfd[1]);
	threadexits(0);
}




void
waiter(void *client)
{
	Client *c = (Client *)client;
	struct Waitmsg *m;

	char *pid;


	/* Then wait in a loop
	  * for all children to be gone. Then set EOF 
	  * and exit 
	  */
	threadsetname("waiter");
	while (m = wait()) {
		debug(DBG_CLIENT, "C%d:waiter: exit: %d %s\n", c->num,  m->pid, m->msg);
		if(c->waitstr) {
			pid = smprint("%d: ", m->pid);
			c->waitstr = s_append(c->waitstr, pid);
			c->waitstr = s_append(c->waitstr, m->msg);
			c->waitstr = s_append(c->waitstr, "\n");
			free(pid);
		}
	}
	debug(DBG_CLIENT, "C%d:waiter: runit has exited\n", c->num);
	threadexits(nil);
}

void
runpty(Req *r, Client *c){
	extern void runit(void *);

	pipe(c->stdinfd);
	pipe(c->stdoutfd);
	pipe(c->stderrfd);

	c->kidpid = proccreate(runit, (void *)c, Stack); 

	if (c->kidpid < 0) {
		werrstr( "runpty: forktpy failed");
		return;
	}


	/* we have a kid */
	/* we only let you run one at a time. This is fine. 
	 */


	debug(DBG_CLIENT,"runpty: fork: kid is %d; command: %s\n", c->kidpid, c->tmpname);
	
	/* now, fire off a waiter to hold the stdout file
	 * open. 
	 */
	c->waiterpid = proccreate(waiter, (void *)c, Stack);

	c->stdoutpid = proccreate(newreadstdout, c, Stack);
	c->stderrpid = proccreate(newreadstderr, c, Stack);
	r->ofcall.count = r->ifcall.count;

	respond(r,nil);
	return;
	

}


static void
ctlwrite(Req *r, Client *c)
{
	char *f[TokenMaxArg], *s;
	int nf;

	s = emalloc9p(r->ifcall.count+1);
	memmove(s, r->ifcall.data, r->ifcall.count);
	s[r->ifcall.count] = '\0';

	nf = tokenize(s, f, TokenMaxArg);
	if(nf == 0){
		free(s);
		respond(r, nil);
		return;
	}

	if(cistrncmp(f[0], "exec", 4) == 0){
		if(c->state != Setup)
			goto Badarg;
		runpty(r, c);
		c->state = Established;

		/* no need to respond here, runpty does it */

	} else if(cistrncmp(f[0], "import", 6) == 0) {
	} else if(cistrncmp(f[0], "eof", 3) == 0) {
		/* close out this client. */
		/* note that stdin and stdout pipes are now different, 
		  * so that we can shutdown stdin to the proc but not
		  * shutdown stdout. 
		  */
		stdineof(c);

		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	} else if(cistrncmp(f[0], "type", 5) == 0) {
		if(nf != 2)
			goto Badarg;

		/* set the state of the session:
		 * normal: close once all open files are closed
		 * persistent: keep the session
		 */
		if(cistrncmp(f[1], "normal", 6) == 0) {
print("ctlrwrite: normal: %s %s\n", f[0], f[1]);
			c->type = Normal;
		} else if(cistrncmp(f[1], "persistent", 10) == 0) {
print("ctlrwrite: persistent: %s %s\n", f[0], f[1]);
			c->type = Persistent;
		} else {
print("ctlrwrite: badarg: %s %s\n", f[0], f[1]);
			goto Badarg;
		}
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
	} else {
Badarg:
		respond(r, "bad or inappropriate control message");
	}
	free(s);
}

static void
lastpidread(Req *r, Client *c)
{
	char buf[32];

	sprint(buf, "%d", c->lastpid);
	readstr(r, buf);
	respond(r, nil);
}

static void
waitread(Req *r, Client *c)
{
	if(c->waitstr)
		readstr(r, s_to_c(c->waitstr));
	respond(r, nil);
}

static void
argvread(Req *r, Client *c)
{
	if (c->argv)
		readstr(r, c->argv);
	else
		readstr(r, "");
	respond(r, nil);
}


static void
argvwrite(Req *r, Client *c)
{
	if (c->argv)
		free(c->argv);
	c->argv = emalloc9p(r->ifcall.count+1);
	debug(DBG_FS, "argvwrite: string pointer: %p\n", c->argv);
	if (c->argv){
		memmove(c->argv, r->ifcall.data, r->ifcall.count);
		c->argv[r->ifcall.count] = '\0';
		debug(DBG_FS, "argvwrite: string %s\n", c->argv);
	}

	r->ofcall.count = r->ifcall.count;

	respond(r,  nil);
}

static void
archread(Req *r, Client *c)
{
	USED(c);

	readstr(r, getarch());
	respond(r, nil);
}

static void
addrread(Req *r, Client *c)
{
	USED(c);
	readstr(r, getarch());
	respond(r, "fix me");
}

static void
unameread(Req *r, Client *c)
{

	USED(c);
	readstr(r, getuname());
	respond(r, nil);
}

static void
dataread(Req *r, Client *c)
{
	int amt = -1;

	if(c->state != Setup){
		respond(r, "not in Setup state");
		return;
	}
	if(r->ifcall.count){
		amt = pread(c->tmpfd, r->ifcall.data, 
					r->ifcall.count, r->ifcall.offset);
		if (amt < 0){
			char err[128];
			/* aw shit. */
			c->state = Setup;
			(void) close(c->tmpfd);
			free(c->tmpname);
			errstr(err, sizeof(err));
//			seterrstr(err);
			respond(r, err);
			return;
		}
	}
	r->ofcall.count = amt;
	respond(r, nil);
}

static void
datawrite(Req *r, Client *c)
{
	int amt = -1;

	if(c->state != Setup){
		respond(r, "not connected");
		return;
	}

	if(r->ifcall.count){
		amt = pwrite(c->tmpfd, r->ifcall.data, 
					r->ifcall.count, r->ifcall.offset);
		if (amt < 0){
			char err[128];
			/* aw shit. */
	//		c->state = Setup;
			(void) close(c->tmpfd);
			free(c->tmpname);
			errstr(err, sizeof(err));
//			seterrstr(err);
			respond(r, err);
			return;
		}
	}
	r->ofcall.count = amt;
	respond(r, nil);
}



static void
stdinwrite(Req *r, Client *c)
{
	int amt = -1;
	debug(DBG_FS, "stdinwrite\n");
	if(c->state != Established){
		respond(r, "connection not in established state");
		return;
	}
	if(r->ifcall.count){
		debug(DBG_FS, "stdinwrite: pwrite %d, %p, %d\n", 
			c->stdinfd[1], r->ifcall.data, r->ifcall.count);
		amt = write(c->stdinfd[1], r->ifcall.data, 
					r->ifcall.count);
		debug(DBG_FS, "stdinwrite: return from write %d\n", 
				amt);
		if (amt < 0){
			char err[128];
			/* aw shit. */
			errstr(err, sizeof(err));
//			seterrstr(err);
			respond(r, err);
			return;
		}
	}
	r->ofcall.count = amt;
	respond(r, nil);
}

static void
statusread(Req *r, Client *c)
{
	char *s;
	s = smprint("%s: ref %d, num %d, gen %d, nclient %d\n", statestr[c->state],c->ref, c->num, c->gen, nclient);
	readstr(r, s);
	respond(r, nil);
	free(s);
}

static void
xread(Req *r)
{
	char e[ERRMAX];
	ulong path;
	Client *c = nil;

	path = r->fid->qid.path;
	debug(DBG_FS, "xread\n");
	/* cover linux bogosity */
	if (TYPE(path) >= Qn) {
		if (client[PATHINDEX(path)]->state == Closed) {
		    debug(DBG_FS, "xread: handle bogus linux cache case\n");
		    snprint(e, sizeof e, "No such file or directory"); 
		    respond(r, e);
		    return;
		}

	}

	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in xread path=%lux", path);
		respond(r, e);
		break;

	case Qroot:
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;

	case Qarch:
		archread(r, client[PATHINDEX(path)]);
		break;

	case Qaddr:
		addrread(r, client[PATHINDEX(path)]);
		break;

	case Quname:
		unameread(r, client[PATHINDEX(path)]);
		break;

	case Qn:
		dirread9p(r, clientgen, client[PATHINDEX(path)]);
		respond(r, nil);
		break;

	case Qctl:
		ctlread(r, client[PATHINDEX(path)]);
		break;

	case Qexec:
		dataread(r, client[PATHINDEX(path)]);
		break;

	case Qargv:
		argvread(r, client[PATHINDEX(path)]);
		break;

	case Qstatus:
		statusread(r, client[PATHINDEX(path)]);
		break;

	case Qprocs:
		procsread(r);
		break;

	case Qstdout:
		reqdump(r, "xread");
		c = client[PATHINDEX(path)];
		queuereq(client[PATHINDEX(path)], r);
		/* implement non-blocking semantics for test */
		/* it seems this one is ok ... it is the matchmsgs from the i/o task? */
//		matchreqmsg(client[PATHINDEX(path)], r);

		matchmsgs(client[PATHINDEX(path)], 0);
//		respond(r, nil);

		break;

	case Qstderr:
		reqdump(r, "xread");
		c = client[PATHINDEX(path)];
		queuereq(client[PATHINDEX(path)], r);
		/* implement non-blocking semantics for test */
		/* it seems this one is ok ... it is the matchmsgs from the i/o task? */
//		matchreqmsg(client[PATHINDEX(path)], r);

		matchmsgs(client[PATHINDEX(path)], 1);
//		respond(r, nil);
		break;

	case Qlastpid:
		lastpidread(r, client[PATHINDEX(path)]);
		break;

	case Qwait:
		waitread(r, client[PATHINDEX(path)]);
		break;
	}
}

static void
xwrite(Req *r)
{
	ulong path;
	char e[ERRMAX];

	path = r->fid->qid.path;

	/* cover linux bogosity */
	if (TYPE(path) >= Qn) {
	  if (client[PATHINDEX(path)]->state == Closed) {
	    debug(DBG_FS, "xwrite: handle bogus linux cache case\n");
	    snprint(e, sizeof e, "No such file or directory"); 
	    respond(r, e);
	    return;
	  }

	}


	switch(TYPE(path)){
	default:
		snprint(e, sizeof e, "bug in xwrite path=%lux", path);
		respond(r, e);
		break;

	case Qctl:
		ctlwrite(r, client[PATHINDEX(path)]);
		break;

	case Qargv:
		argvwrite(r, client[PATHINDEX(path)]);
		break;

	case Qexec:
		datawrite(r, client[PATHINDEX(path)]);
		break;

	case Qprocs:
		procswrite(r, client[PATHINDEX(path)]);
		break;

	case Qstdin:
		stdinwrite(r, client[PATHINDEX(path)]);
		break;
	}
}

static void
xopen(Req *r)
{
	void tmpexec(Client *);
	char err[128];
	static int need[4] = { 4, 2, 6, 1 };
	ulong path;
	int n;
	Tab *t;
	Client *c;
	/*
	 * lib9p already handles the blatantly obvious.
	 * we just have to enforce the permissions we have set.
	 */
	path = r->fid->qid.path;
	t = &tab[TYPE(path)];
	n = need[r->ifcall.mode&3];
	if((n&t->mode) != n){
		respond(r, "permission denied");
		return;
	}

	switch(TYPE(path)){
	case Qexec:
		c = client[PATHINDEX(path)];
		tmpexec(c);
		if (c->tmpfd < 0){
			errstr(err, sizeof(err));
//			seterrstr(err);
			respond(r, err);
			return;
		}

		break;
	case Quname:
		break;
	case Qarch:
		break;
	case Qprocs:
		break;
	case Qclone:
		c = client[newclient()];
		path = PATH(Qctl, c->num, c->gen);
		r->fid->qid.path = path;
		r->ofcall.qid.path = path;
		if(chatty9p)
			debug(DBG_FS, "xopen: open clone => path=%lux\n", path);
		t = &tab[Qctl];
		debug(DBG_FS, "xopen: t %p, t - tab %ld, Qn %d\n", t, t-tab, Qn);
		debug(DBG_FS, "xopen: NUM(path) is 0x%x\n", NUM(path));
		break;
	}

	if(t-tab >= Qn){
		client[PATHINDEX(path)]->ref++;
		debug(DBG_FS, "C%d:xopen: ptr is %p, REF: %d, file %s\n", 
					client[PATHINDEX(path)]->num,
					client[PATHINDEX(path)], 
					client[PATHINDEX(path)]->ref,
					tab[TYPE(path)].name);
	}
	respond(r, nil);
}

static void
xflush(Req *r)
{
	debug(DBG_FS, "xflush\n");
	respond(r, nil);
}

static void
handlemsg(Msg *m)
{
	int n;
	Client *c;
	debug(DBG_FS, "handlemsg\n");
	debug(DBG_FS, "handlemsg: type %x\n", m->type);
	msgdump(m, "stdio msg");
	switch(m->type){
	case XCPU_MSG_STDOUT:
	case XCPU_MSG_STDERR:
		c = m->c;
		n = m->ep - m->rp;
		debug(DBG_FS, "handlemsg: for client got %d bytes\n", n);
		if(c->state==Established){
			queuemsg(c, m->type, m);
			debug(DBG_FS, "handlemsg: warning! matchmsg turned on in handlemsg data case you may hang\n");
			matchmsgs(c, m->type);
		}else
			free(m);
		break;
			
		case XCPU_MSG_STDOUT_EOF:
		case XCPU_MSG_STDERR_EOF:
		c = m->c;
		/* queue up a zero-byte read so the client sees an EOF */
		if(c->state==Established){
			queuemsg(c, m->type - XCPU_MSG_STDOUT_EOF,m);
			/* NOTE: if we enable this, we get 
			  * hangs. This is really weird. 
			  * Andrey, save me!
			  */
			matchmsgs(c,m->type - XCPU_MSG_STDOUT_EOF);
		}else
			free(m);
//		hangupclient(c);
		break;

	default:
		break;
	}
}

void
fsnetproc(void*v)
{
	ulong path;
	Alt a[4];
	Fid *fid;
	Req *r;
	Msg *m;
	USED(v);
	threadsetname("fsthread");
	debug(DBG_FS, "fsnetproc: pid is %d\n", getpid());
	a[0].op = CHANRCV;
	a[0].c = fsclunkchan;
	a[0].v = &fid;
	a[1].op = CHANRCV;
	a[1].c = fsreqchan;
	a[1].v = &r;
	a[2].op = CHANRCV;
	a[2].c = xcpumsgchan;
	a[2].v = &m;

	a[3].op = CHANEND;


	for(;;){
		switch(alt(a)){
		case 0:
			debug(DBG_FS, "fsnetproc: clunk traffic\n");
			path = fid->qid.path;
			switch(TYPE(path)){
			default:
				break;
			}
			if(fid->omode != -1 && TYPE(path) >= Qn)
				closeclient(client[PATHINDEX(path)], TYPE(path));
			sendp(fsclunkwaitchan, nil);
			break;
		case 1:
			debug(DBG_FS, "fsnetproc: normal traffic\n");
			switch(r->ifcall.type){
			case Tattach:
				xattach(r);
				break;
			case Topen:
				xopen(r);
				break;
			case Tread:
				xread(r);
				break;
			case Twrite:
				xwrite(r);
				break;
			case Tstat:
				xstat(r);
				break;
			case Twstat:
				xwstat(r);
				break;
			case Tflush:
				xflush(r);
				break;
			default:
				respond(r, "bug in fsthread");
				break;
			}
			debug(DBG_FS, "fsnetproc: done 9p op\n");
			sendp(fsreqwaitchan, 0);
			debug(DBG_FS, "fsnetproc: done sendp\n");
			break;
		case 2:
			debug(DBG_FS, "fsnetproc: something from readconosle\n");
			handlemsg(m);
			break;
		}
	}
	debug(DBG_FS, "fsnetproc: leaving\n");
}

static void
fssend(Req *r)
{
//	debug(DBG_FS, "(%d)fssend: send\n", getpid());
	sendp(fsreqchan, r);
//	debug(DBG_FS, "fssend:recv\n");
	recvp(fsreqwaitchan);	/* avoids need to deal with spurious flushes */
//	debug(DBG_FS, "fssend: done\n");
}

static void
fsdestroyfid(Fid *fid)
{
	sendp(fsclunkchan, fid);
	recvp(fsclunkwaitchan);
}

void
takedown(Srv*v)
{
	USED(v);
	debug(DBG_FS, "takedown\n");
	threadexitsall(nil);
}

Srv fs = 
{
	.attach=		fssend,
	.destroyfid=	fsdestroyfid,
	.walk1=		fswalk1,
	.open=		fssend,
	.read=		fssend,
	.write=		fssend,
	.stat=		fssend,
	.flush=		fssend,
	.wstat=		fssend,
	.end=		takedown,
};

void
threadmain(int argc, char **argv)
{
	char *service;
	int i;

    time0 = time(0);
    mtime = time0;
	/* some default user name file owners */
	if(uid == nil)
		uid = estrdup9p(defuid);
	if(gid == nil)
		gid = estrdup9p(defgid);
	muid = estrdup9p(defmuid);
    mode = 0440;    /* mode for Qclone */

	fmtinstall('D', dirfmt);

	if(argc < 2)
		usage();

	service = nil;
	ARGBEGIN{
	case 'B':	/* undocumented, debugging */
		doabort = 1;
		break;
	case 'u':	/* user id (string) */
		free(uid);
		uid = estrdup9p(EARGF(usage()));
		break;
	case 'g':	/* group id (string) */
		free(gid);
		gid = estrdup9p(EARGF(usage()));
		break;
	case 'U':	/* uid, numeric */
		nuid = strtol(EARGF(usage()), nil, 0);
		break;
	case 'G':	/* gid, numeric */
		ngid = strtol(EARGF(usage()), nil, 0);
		break;
	case 'D':	/* undocumented, debugging */
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	case '9':	/* undocumented, debugging */
		chatty9p++;
		break;
	case 's':
		service = EARGF(usage());
		break;
	case 'r':
		reghost = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc)
		usage();

	/* initialize the array of strings reported by /procs */
	for(i = 0; i < proclast; i++) {
		procstr[i] = i;
	}



	xcpumsgchan = chancreate(sizeof(Msg*), 16);
	fsreqchan = chancreate(sizeof(Req*), 0);
	fsreqwaitchan = chancreate(sizeof(void*), 0);
	fsclunkchan = chancreate(sizeof(Fid*), 0);
	fsclunkwaitchan = chancreate(sizeof(void*), 0);

	proccreate(fsnetproc, nil, Stack);

	/* TODO: add a process that registers itself at a control node */

	threadpostmountsrv(&fs, service, nil, MREPL|MCREATE);
	threadexits(0);
}
