#include "xsrv.h"
#include <libString.h>
  
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <errno.h>

/* our little implementation of threadspawn which drops permissions */
typedef struct Execjob Execjob;
struct Execjob
{
	int *fd;
	char *cmd;
	char **argv;
	Client *cl;
	Channel *c;
};

int
_threadspawn(Client *c, int fd[3], char *cmd, char *argv[])
{
	int i, n, p[2], pid;
	char exitstr[100];

	if(pipe(p) < 0)
		return -1;
	if(fcntl(p[0], F_SETFD, 1) < 0 || fcntl(p[1], F_SETFD, 1) < 0){
		close(p[0]);
		close(p[1]);
		return -1;
	}
	switch(pid = fork()){
	case -1:
		close(p[0]);
		close(p[1]);
		return -1;
	case 0:
		/* this is what we came here to do! */
		if(s_len(c->env) > 0)
			oscenvw(c->env);

		dup2(fd[0], 0);
		dup2(fd[1], 1);
		dup2(fd[2], 2);
		for(i=3; i<100; i++)
			if(i != p[1])
				close(i);
		setuid(c->nuid);
		setgid(c->ngid);
		execvp(cmd, argv);
		fprint(p[1], "%d", errno);
		close(p[1]);
		_exit(0);
	}

	close(p[1]);
	n = read(p[0], exitstr, sizeof exitstr-1);
	close(p[0]);
	if(n > 0){	/* exec failed */
		exitstr[n] = 0;
		errno = atoi(exitstr);
		return -1;
	}

	close(fd[0]);
	if(fd[1] != fd[0])
		close(fd[1]);
	if(fd[2] != fd[1] && fd[2] != fd[0])
		close(fd[2]);
	return pid;
}

static void
execproc(void *v)
{
	int pid;
	Execjob *e;
	Waitmsg *w;
	Req *r;

	e = (Execjob *)v;
	pid = _threadspawn(e->cl, e->fd, e->cmd, e->argv);
	sendul(e->c, pid);
	if(pid > 0){
		while((w = wait()) != nil) {
			if(w->msg[0] != 0 && 
				w->pid != e->cl->outpid &&
				w->pid != e->cl->inpid &&
				w->pid != e->cl->errpid) {
					if(e->cl->wstr)
						s_append(e->cl->wstr, w->msg);
			}
			free(w);
		}
		while((r = recvq(e->cl->waitq)) != nil)
			readstr(r, s_to_c(e->cl->wstr));
	}

	chanfree(e->c);
	free(e);
	threadexits(nil);
}

int
osfork(Client *c, int fd[], char *cmd, char **argv)
{
	int pid;
	Execjob *e;

	e = malloc(sizeof(Execjob)); /* freed in execproc() */
	if(e == nil)
		return -1;
	e->fd = fd;
	e->cmd = cmd;
	e->argv = argv;
	e->cl = c;
	e->c = chancreate(sizeof(void*), 0); /* freed in execproc() */
	if(e->c == nil) {
		free(e);
		return -1;
	}
	proccreate(execproc, e, 65536);
	pid = recvul(e->c);
	return pid;
}


extern int proclast;
extern char *procnames[];
extern int procstr[];

/* we accept a string (sexp?) of values we care for reporting */
void
procswrite(Req *r)
{
	String *s;
	char *ptoks[proclast];
	char *chr;
	int i, j, toks, tmppstr[proclast];

	s = s_new();
	if(s == nil)
		respond(r, "can not allocate memory for data");

	/* BUG: we're assuming we fit a whole description in a single packet */
	s_nappend(s, r->ifcall.data, r->ifcall.count);

	/* flatten the sexp, it's not nested anyway, or shouldn't be */
	if((chr = strchr(s_to_c(s), '(')) != nil)
		*chr = ' ';
	if((chr = strchr(s_to_c(s), ')')) != nil)
		*chr = ' ';

	
	toks = tokenize(s_to_c(s), ptoks, proclast);
	if(toks == 1 && strcmp(ptoks[0], "all") == 0) {
		for(i = 0; i < proclast; i++)
			tmppstr[i] = i;
	} else for(i = 0; i < toks; i++) {
		tmppstr[i] = -1;
		for(j = 0; j < proclast; j++) {
			if(strcmp(ptoks[i], procnames[j]) == 0)
				tmppstr[i] = j;
		}
		if(tmppstr[i] == -1) {
			s_free(s);
			s = s_new();
			s_append(s, "invalid string descriptor: ");
			s_append(s, ptoks[i]);
			s_append(s, "; transaction cancelled");
			respond(r, s_to_c(s));
			s_free(s);
			return;
		}
	}
	if(i < proclast)
		tmppstr[i] = -1;	/* terminate the list */

	for(i = 0; i < proclast; i++)
		procstr[i] = tmppstr[i]; 

	s_free(s);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

int 
checktemp(void) 
{
	char *tmp = smprint("/tmp/xcpuXXXXXX");
	int fd;

	debug(Dbgfn, "checktemp\n");
	fd = mkstemp(tmp);
	close(fd);
	remove(tmp);
	free(tmp);
	return fd;
}

int 
maketemp(Client *c) 
{
	debug(Dbgfn, "maketemp\n");
	c->tmpname = smprint("/tmp/xcpuXXXXXX");
	return mkstemp(c->tmpname);
}

int
oschmod(char *file, ulong mode)
{
	return chmod(file, mode);
}

void
dotufill(Srv *fs, Dir *d, int uidnum, int gidnum, char *ext)
{
	if(fs->dotu) {
		d->uidnum = uidnum;
		d->gidnum = gidnum;
		if(ext)
			d->ext = ext;
	}
	return;
}

extern uint nuid;
extern uint ngid;
void
dotuwfill(Dir *d)
{
	if(fs.dotu) {
		if(d->uidnum != (~0))
			nuid = d->uidnum;
		if(d->gidnum != (~0))
			ngid = d->gidnum;
	}
}


int
oschown(Client *c)
{
	return chown(c->tmpname, c->nuid, c->ngid);
}

extern char **environ;
void
osenvr(void *v)
{
	Req *r = v;
	String *s;
	char **e;

	debug(Dbgfn, "osenvread\n");
	if(environ == nil) {
		respond(r, "Resource temporarily unavailable");
		return;
	}

	s = s_new();
	if(s == nil) {
		respond(r, "Resource temporarily unavailable");
		return;
	}
	for(e = environ; *e != nil; e++) {
		s_append(s, *e);
		s_append(s, "\n");
	}
	readstr(r, s_to_c(s));
	respond(r, nil);
	s_free(s);
	threadexits(0);
}

void
osenvw(Req *r)
{
	char *str, *nstr, *tmp;
	int cnt;

	debug(Dbgfn, "osenvwrite\n");
	/* rethink this: what happens if an environment variable crosses
	 * boundaries?
	 */
	cnt = r->ifcall.count;
	str = r->ifcall.data;
	nstr = str;
	while(cnt > 0) {
		if(*nstr == '\n') {
			*nstr = '\0';
			tmp = strchr(str, '=');
			if(tmp != nil)
				*tmp++ = '\0';	
			putenv(str, tmp);
			str = nstr + 1;
		}
		cnt--;
		nstr++;
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
	return;
}

void
oscenvw(String *s)
{
	char *str, *nstr, *tmp;

	str = s_to_c(s);
	nstr = str;
	do{
            if(*nstr == '\n' || *nstr == '\0') {
			*nstr = '\0';
			tmp = strchr(str, '=');
			if(tmp == nil)
                            break;
                        else{
                            *tmp = '\0';
                            tmp++;
                            if(str && tmp){
                                putenv(str, tmp);
                            }
                            str = nstr+1;
                        }
		}
                if(nstr==nil)
                    break;
                else
                    nstr++;
	}while(1);
        return;
}

void
osdaemon(void)
{
	daemon(0, 0);
}

CFsys*
xparse(char *name)
{
	int fd;
	CFsys *fs;

	if((fd = dial(name, nil, nil, nil)) < 0) {
		werrstr("dial: %r");
		return nil;
	}
	if((fs = fsmount(fd, nil)) == nil)
		werrstr("ERROR: fsamount: %r");

	return fs;
}


/* copy to a remote server over 9p */
int
fscopyto(int from, CFid *to) 
{
	int r, w, sum = 0;
	char *buf;

	buf = malloc(Bufsize);
	if(buf == nil) {
		werrstr("fscopyto: malloc: %r");
		return -1;
	}

	while ((r = read(from, buf, Bufsize)) > 0) {
		w = fswrite(to, buf, r);
		if (w != r)
			return -1;
		sum += w;
	}
	return sum;
}

void
osspawner(void *v) 
{
	Spawner *sp = v;
	CFsys *fs;
	CFid *wfd;
	Client *c;
	char *name, *addr;
	char *sid, *err = nil;
	int rfd;

	addr = sp->path;
	c = sp->c;

	/* expecting string format: tcp!addr!port:sessionid */
	sid = strchr(addr, ':');
	if(sid == nil) {
		err = smprint("no session id");
		goto Donespawn;
	}

	*sid++ = '\0';

	if(strchr(addr, '!') == nil)
		addr = netmkaddr(addr, "tcp", "20001");
	fs = xparse(addr); 
	if(fs == nil) {
		err = smprint("fs: %r");
		goto Donespawn;
	}

	rfd = open(c->tmpname, OREAD);
	if (rfd < 0) {
		err = smprint("rfd: %s: %r", c->tmpname);
		goto Donespawn;
	}

	name = smprint("/%s/exec", sid);
	if(name == nil) {
		err = "smprint";
		goto Donespawn;
	}

	wfd = fsopen(fs, name, OWRITE);
	if (wfd == nil) {
		free(name);
		err = smprint("wfid: %s: %r", name);
		goto Donespawn;
	}

	if(fscopyto(rfd, wfd) < 0)
		err = smprint("fscopyto: %r");
	close(rfd);
	fsclose(wfd);
	free(name);

	debug(Dbgspawn, "[%d]: spawner: osspawn: copied binary to: %s/%s\n", sp->c->num, sp->path, sid);

	if(sp->spawn != nil) {
		name = smprint("/%s/ctl", sid);
		if(name == nil) {
			err = "smprint (ctl)";
			goto Donespawn;
		}
		wfd = fsopen(fs, name, OWRITE);
		if (wfd == nil) {
			free(name);
			err = smprint("wfid (ctl): %s: %r", name);
			goto Donespawn;
		}
		debug(Dbgspawn, "[%d]: spawner: osspawn: spawning to: %s/%s, list: %s\n", sp->c->num, sp->path, sid, s_to_c(sp->spawn));
		fswrite(wfd, s_to_c(sp->spawn), strlen(s_to_c(sp->spawn)));
		fsclose(wfd);
		free(name);
	}

Donespawn:
	if(err) {
		werrstr(err);
		sendul(sp->chan, -1);
	} else {
		sendul(sp->chan, 0);
	}
	threadexits(err);
}

int
osgetuid(char *name)
{
	struct passwd *p;
	int ret = -1;

	if(name == nil)
		return -1;

	while((p = getpwent()) != nil) {
		if(strcmp(name, p->pw_name) == 0) {
			ret = p->pw_uid;
			goto Uidone;
		}
	}
Uidone:
	endpwent();
	return ret;
}
