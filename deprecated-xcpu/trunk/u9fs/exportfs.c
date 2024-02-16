#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>

char *dir;
ulong time0;

char    Eauth[] =   "authentication failed";
char    Ebadfid[] = "fid unknown or out of range";
char    Ebadoffset[] =  "bad offset in directory read";
char    Ebadusefid[] =  "bad use of fid";
char    Edirchange[] =  "wstat can't convert between files and directories";
char    Eexist[] =  "file or directory already exists";
char    Efidactive[] =  "fid already in use";
char    Enotdir[] = "not a directory";
char    Enotingroup[] = "not a member of proposed group";
char    Enotowner[] =   "only owner can change group in wstat";
char    Eperm[] =   "permission denied";
char    Especial0[] =   "already attached without access to special files";
char    Especial1[] =   "already attached with access to special files";
char    Especial[] =    "no access to special file";
char    Etoolarge[] =   "i/o count too large";
char    Eunknowngroup[] = "unknown group";
char    Eunknownuser[] = "unknown user";
char    Ewstatbuffer[] = "bogus wstat buffer";

typedef struct Tab Tab;
struct Tab
{
	char *name;
	vlong qid;
	ulong time;
	int ref;
};

typedef struct FFid FFid;
struct FFid {
	int fid;
	char *path;
	struct stat st;
	int omode;
	Dir *dir;
	int diroffset;
	int fd;
	struct dirent *dirent;
	FFid *next;
	FFid *prev;
	int auth;
	void *authmagic;
};

Tab *tab;
int ntab;
int mtab;

static void
usage(void)
{
	fprint(2, "%s [-s srvname] [-p port] [dir]\n", argv0);
	threadexits("usage");
}

static Tab*
findtab(vlong path)
{
	int i;

	for(i=0; i<ntab; i++)
		if(tab[i].qid == path)
			return &tab[i];
	return nil;
}

FFid *fidtab[1];

FFid*
lookupfd(int fid)
{
    FFid *f;

    for(f=fidtab[fid%nelem(fidtab)]; f; f=f->next)
        if(f->fid == fid)
            return f;
    return nil;
}

FFid*
newfid(int fid, char **ep)
{
    FFid *f;

    if(lookupfd(fid) != nil){
        *ep = Efidactive;
        return nil;
    }

    f = emalloc9p(sizeof(*f));
    f->next = fidtab[fid%nelem(fidtab)];
    if(f->next)
        f->next->prev = f;
    fidtab[fid%nelem(fidtab)] = f;
    f->fid = fid;
    f->fd = -1;
    f->omode = -1;
    return f;
}

static vlong
hash(char *name)
{
	vlong digest[MD5dlen / sizeof(vlong) + 1];
	md5((uchar *)name, strlen(name), (uchar *)digest, nil);
	return digest[0] & ((1ULL<<48)-1);
}

static void
fsopen(Req *r)
{
	if(r->ifcall.mode != OREAD)
		respond(r, "permission denied");
	else
		respond(r, nil);
}

static int
dirgen(int i, Dir *d, void *unused)
{
	if(i >= ntab)
		return -1;
	memset(d, 0, sizeof *d);
	d->qid.type = QTDIR;
	d->uid = estrdup9p("sys");
	d->gid = estrdup9p("sys");
	d->mode = DMDIR|0555;
	d->length = 0;
	if(i == -1){
		d->name = estrdup9p("/");
		d->atime = d->mtime = time0;
	}else{
		d->qid.path = tab[i].qid;
		d->name = estrdup9p(tab[i].name);
		d->atime = d->mtime = tab[i].time;
	}
	return 0;
}

static void
fsread(Req *r)
{
	if(r->fid->qid.path == 0)
		dirread9p(r, dirgen, nil);
	else
		r->ofcall.count = 0;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	Tab *t;
	vlong qid;

	qid = r->fid->qid.path;
	if(qid == 0)
		dirgen(-1, &r->d, nil);
	else{
		if((t = findtab(qid)) == nil){
			respond(r, "path not found");
			return;
		}
		dirgen(t-tab, &r->d, nil);
	}
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, void *unused)
{
	int i;
	Tab *t;
	vlong h;

	if(fid->qid.path != 0){
		/* nothing in child directory */
		if(strcmp(name, "..") == 0){
			if((t = findtab(fid->qid.path)) != nil)
				t->ref--;
			fid->qid.path = 0;
			return nil;
		}
		return "path not found";
	}
	/* root */
	if(strcmp(name, "..") == 0)
		return nil;
	for(i=0; i<ntab; i++)
		if(strcmp(tab[i].name, name) == 0){
			tab[i].ref++;
			fid->qid.path = tab[i].qid;
			return nil;
		}
	h = hash(name);
	if(findtab(h) != nil)
		return "hash collision";

	/* create it */
	if(ntab == mtab){
		if(mtab == 0)
			mtab = 16;
		else
			mtab *= 2;
		tab = erealloc9p(tab, sizeof(tab[0])*mtab);
	}
	tab[ntab].qid = h;
	fid->qid.path = tab[ntab].qid;
	tab[ntab].name = estrdup9p(name);
	tab[ntab].time = time(0);
	tab[ntab].ref = 1;
	ntab++;

	return nil;
}

static char*
fsclone(Fid *fid, Fid *unused1, void *unused2)
{
	Tab *t;

	if((t = findtab(fid->qid.path)) != nil)
		t->ref++;
	return nil;
}

static void
fswalk(Req *r)
{
	walkandclone(r, fswalk1, fsclone, nil);
}

static void
fsclunk(Fid *fid)
{
	Tab *t;
	vlong qid;

	qid = fid->qid.path;
	if(qid == 0)
		return;
	if((t = findtab(qid)) == nil){
		fprint(2, "warning: cannot find %llux\n", qid);
		return;
	}
	if(--t->ref == 0){
		free(t->name);
		tab[t-tab] = tab[--ntab];
	}else if(t->ref < 0)
		fprint(2, "warning: negative ref count for %s\n", t->name);
}

int
fidstat(FFid *fid, char **ep)
{
    if(stat(fid->path, &fid->st) < 0){
        fprint(2, "fidstat(%s) failed\n", fid->path);
        if(ep)
            *ep = strerror(errno);
        return -1;
    }
    if(S_ISDIR(fid->st.st_mode))
        fid->st.st_size = 0;
    return 0;
}

void
rattach(Req *r)
{
	char *spec;
    char *e;
    FFid *fid;

	spec = r->ifcall.aname;
	if(spec && spec[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	
    if((fid = newfid(r->ifcall.fid, &e)) == nil){
        respond(r, e);
        return;
    }
    fid->path = estrdup9p("/");
    if(fidstat(fid, &e) < 0){
        respond(r, e);
        freefid(fid);
        return;
    }

    if(defaultuser)
        r->ifcall.uname = defaultuser;

//    if((u = uname2user(r->uname, -1)) == nil
//    || (!defaultuser && u->id == 0)){
        /* we don't know anyone named root... */
//        respond(r, Eunknownuser);
//        freefid(fid);
//        return;
//    }

    fid->u = u;
    r->qid = stat2qid(&fid->st);
	respond(r, nil);
}

void
rauth(Req *r)
{
    char *e;

    e = auth->auth(r);
	respond(r, e);
}

void
ropen(Req *r)
{
    char *e;
    FFid *fid;

    if((fid = oldfid(r->fid, &e)) == nil){
        respond(r, e);
        return;
    }

    if(fid->omode != -1){
        respond(r, Ebadusefid);
        return;
    }

    if(fidstat(fid, &e) < 0){
        respond(r, e);
        return;
    }

    if(!devallowed && S_ISSPECIAL(fid->st.st_mode)){
        respond(r, Especial);
        return;
    } 

    if(useropen(fid, r->mode, &e) < 0){
        respond(r, e);
        return;
    }

    r->iounit = 0;
    r->qid = stat2qid(&fid->st);
	respond(r, nil);
}

void
rcreate(Req *r)
{
    char *e;
    FFid *fid;

    if((fid = oldfid(r->fid, &e)) == nil){
        respond(r, e);
        return;
    }

    if(fid->omode != -1){
        respond(r, Ebadusefid);
        return;
    }

    if(fidstat(fid, &e) < 0){
        respond(tx, e);
        return;
    }

    if(!S_ISDIR(fid->st.st_mode)){
        respond(r, Enotdir);
        return;
    }

    if(usercreate(fid, r->name, r->mode, r->perm, &e) < 0){
        respond(r, e);
        return;
    }

    if((extended)&&(r->perm == DMSYMLINK)) {
        r->ofcall.iounit = 0;
        r->ofcall.qid.type = QTSLINK;
        r->ofcall.qid.vers = ~0;
        r->ofcall.qid.path = ~0;
    } else if ((extended)&&(r->perm == DMLINK)) {
        r->ofcall.iounit = 0;
        r->ofcall.qid.type = QTLINK;
        r->ofcall.qid.vers = ~0;
        r->ofcall.qid.path = ~0;
    } else if ((extended)&&(r->perm & DMDEVICE)) {
        r->ofcall.iounit = 0;
        r->ofcall.qid.type = QTTMP; /* not really */
        r->ofcall.qid.vers = ~0;
        r->ofcall.qid.path = ~0;
    } else {
        if(fidstat(fid, &e) < 0){
            respond(r, e);
            return;
        }
        r->ofcall.iounit = 0;
        r->ofcall.qid = stat2qid(&fid->st);
    }
	respond(r, nil);
}

void
rread(Req *r)
{
    char *e, *path;
    uchar *p, *ep;
    int n;
    FFid *fid;
    Dir d;
    struct stat st;

    if(r->ifcall.count > msize-IOHDRSZ){
        fprint(2, "count too big\n");
        respond(r, Etoolarge);
        return;
    }

    if((fid = oldfidex(r->ifcall.fid, -1, &e)) == nil){
        respond(r, e);
        return;
    }

    if (fid->auth) {
        char *e;
        e = auth->read(r);
        if (e)
            respond(r, e);
        return;
    }

    if(fid->omode == -1 || (fid->omode&3) == OWRITE){
        respond(r, Ebadusefid);
        return;
    }

    if(fid->dir){
        if(r->ifcall.offset != fid->diroffset){
            if(r->ifcall.offset != 0){
                respond(r, Ebadoffset);
                return;
            }

            rewinddir(fid->dir);
            fid->diroffset = 0;
        }

        p = (uchar*)r->ofcall.data;
        ep = (uchar*)r->ofcall.data+r->ifcall.count;

        for(;;){
            if(p+BIT16SZ >= ep) {
                break;
            }
            if(fid->dirent == nil)  {
                if((fid->dirent = readdir(fid->dir)) == nil) {
                    break;
                }
            }
            if(strcmp(fid->dirent->d_name, ".") == 0
            || strcmp(fid->dirent->d_name, "..") == 0){
                fid->dirent = nil;
                continue;
            }
            path = estrpath(fid->path, fid->dirent->d_name);
            memset(&st, 0, sizeof st);
            if(stat(path, &st) < 0){
                fprint(2, "dirread: stat(%s) failed: %s\n", path, strerror(errno));
                fid->dirent = nil;
                free(path);
                continue;
            }
            free(path);
            stat2dir(fid->dirent->d_name, &st, &d);
            if((n=(old9p ? convD2Mold : convD2M)(&d, p, ep-p)) <= BIT16SZ)
                break;
            p += n;
            fid->dirent = nil;
        }
        r->ofcall.count = p - (uchar*)r->ofcall.data;
        fid->diroffset += r->ofcall.count;
    }else{
        if((n = pread(fid->fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset)) < 0){
            respond(r, strerror(errno));
            return;
        }
        r->ofcall.count = n;
    }
	respond(r, nil);
}

void
rwrite(Req *r)
{
    char *e;
    FFid *fid;
    int n;

    if(r->ifcall.count > msize-IOHDRSZ){
        respond(r, Etoolarge);
        return;
    }

    if((fid = oldfidex(r->ifcall.fid, -1, &e)) == nil){
        respond(r, e);
        return;
    }

    if (fid->auth) {
        char *e;
        e = auth->write(rx, r);
        if (e)
            respond(r, e);
        return;
    }

    if(fid->omode == -1 || (fid->omode&3) == OREAD || (fid->omode&3) == OEXEC){
        respond(r, Ebadusefid);
        return;
    }

    if((n = pwrite(fid->fd, r->ifcall.data, r->ifcall.count, r->ifcall.offset)) < 0){
        respond(r, strerror(errno));
        return;
    }
    r->ofcall.count = n;
	respond(r, nil);
}

void            
rremove(Req *r)
{
    char *e;
    FFid *fid;

    if((fid = oldfid(r->ifcall.fid, &e)) == nil){
        respond(r, e);
        return;
    }
    if(userremove(fid, &e) < 0)
        respond(r, e);
    freefid(fid);
}

void
rstat(Req *r)
{
    char *e;
    FFid *fid;
    Dir d;

    if((fid = oldfid(r->ifcall.fid, &e)) == nil){
        respond(r, e);
        return;
    }

    if(fidstat(fid, &e) < 0){
        respond(r, e);
        return;
    }

    stat2dir(fid->path, &fid->st, &d);
    if((r->ofcall.nstat=(old9p ? convD2Mold : convD2M)(&d, r->ofcall.stat, msize)) <= BIT16SZ) {
        respond(r, "convD2M fails");
    }
	respond(r, nil);
}


void
rwstat(Req *r)
{
	char *e;
	char *p, *old, *new, *dir;
	uid_t uid;
	gid_t gid;
	Dir d;
	FFid *fid;
	FFid *ofid;
	int old_fid = -1;

	if((fid = oldfid(r->ifcall.fid, &e)) == nil){
		respond(r, e);
		return;
	}

	/*
	 * wstat is supposed to be atomic.
	 * we check all the things we can before trying anything.
	 * still, if we are told to truncate a file and rename it and only
	 * one works, we're screwed.  in such cases we leave things
	 * half broken and return an error.  it's hardly perfect.
	 */
	if((old9p ? convM2Dold : convM2D)(r->ifcall.stat, r->ifcall.nstat, &d, (char*)r->ifcall.stat) <= BIT16SZ){
		respond(r, Ewstatbuffer);
		return;
	}

	if((fid->omode != ~0)&&(fid->omode & DMSYMLINK)) {
		if(d.extension == NULL) {
			respond(r, "No symlink target specified");
			return;
		}

		if (symlink(d.extension, fid->path) < 0) {
			respond(r, strerror(errno));
			return;
		}

		fid->omode = 0;
		return;
	}

	if((fid->omode != ~0)&&(fid->omode & DMLINK)) {
		if(d.extension == NULL) {
			respond(r, "No hardlink target specified");
			return;
		}

		/* pull fid from extension */
		sscanf(d.extension, "hardlink(%d)", &old_fid);
		/* get fid data structure */
		if((ofid = oldfid(old_fid, &e)) == nil){
			respond(r, e);
			return;
		}

		if (link(ofid->path, fid->path) < 0) {
			respond(r, strerror(errno));
			return;
		}

		fid->omode = 0;
		return;
	}

	if((fid->omode != ~0)&&(fid->omode & DMDEVICE)) {
		int major = -1;
	 	int minor = -1;
		char type = 0;

		if(d.extension == NULL) {
			respond(r, "No device info specified");
			return;
		}

		/* pull device info from extension */
		sscanf(d.extension, "%c %u %u", &type, &major, &minor);

		switch(type) {
			case 'c':
				fid->omode = (fid->omode&0777)|S_IFCHR;
				break;
			case 'b':
				fid->omode = (fid->omode&0777)|S_IFBLK;
				break;
			default:
				respond(r, "Invalid device info");
				return;
		};

		d.mode = fid->omode;
		if ( mknod(fid->path, d.mode, MKDEV(major, minor))<0) {
			respond(r, strerror(errno));
			return;
		}

		return;
	}

	if(fidstat(fid, &e) < 0){
		respond(r, e);
		return;
	}

	if((u32int)d.mode != (u32int)~0 && (((d.mode&DMDIR)!=0) ^ (S_ISDIR(fid->st.st_mode)!=0))){
		respond(r, Edirchange);
		return;
	}

	if(strcmp(fid->path, "/") == 0){
		respond(r, "no wstat of root");
		return;
	}

	/*
	 * try things in increasing order of harm to the file.
	 * mtime should come after truncate so that if you
	 * do both the mtime actually takes effect, but i'd rather
	 * leave truncate until last.
	 * (see above comment about atomicity).
	 */
	if((u32int)d.mode != (u32int)~0 && chmod(fid->path, unixmode(&d)) < 0){
		if(chatty9p)
			fprint(2, "chmod(%s, 0%luo) failed\n", fid->path, unixmode(&d));
		respond(r, strerror(errno));
		return;
	}

	if((u32int)d.mtime != (u32int)~0){
		struct utimbuf t;

		t.actime = 0;
		t.modtime = d.mtime;
		if(utime(fid->path, &t) < 0){
			if(chatty9p)
				fprint(2, "utime(%s) failed\n", fid->path);
			respond(r, strerror(errno));
			return;
		}
	}

	if(gid != (gid_t)-1 && gid != fid->st.st_gid){
		if(chown(fid->path, (uid_t)-1, gid) < 0){
			if(chatty9p)
				fprint(2, "chgrp(%s, %d) failed\n", fid->path, gid);
			respond(r, strerror(errno));
			return;
		}
	}

	if(d.name[0]){
		old = fid->path;
		dir = estrdup(fid->path);
		if((p = strrchr(dir, '/')) > dir)
			*p = '\0';
		else{
			respond(r, "whoops: can't happen in u9fs");
			return;
		}
	
		new = estrpath(dir, d.name);
		if(strcmp(old, new) != 0 && rename(old, new) < 0){
			if(chatty9p)
				fprint(2, "rename(%s, %s) failed\n", old, new);
			respond(r, strerror(errno));
			free(new);
			free(dir);
			return;
		}
		fid->path = new;
		free(old);
		free(dir);
	}

	if((u64int)d.length != (u64int)~0 && truncate(fid->path, d.length) < 0){
		fprint(2, "truncate(%s, %lld) failed\n", fid->path, d.length);
		respond(r, strerror(errno));
		return;
	}

	respond(r, nil);
}

void
rwalk(Req *r)
{
	int i;
	char *path, *e;
	FFid *fid, *nfid;

	e = nil;
	if((fid = oldfid(r->ifcall.fid, &e)) == nil){
		respond(r, e);
		return;
	}

	if(fid->omode != -1){
		respond(r, Ebadusefid);
		return;
	}

	if(fidstat(fid, &e) < 0){
		respond(r, e);
		return;
	}

	if(!S_ISDIR(fid->st.st_mode) && r->ifcall.nwname){
		respond(r, Enotdir);
		return;
	}

	nfid = nil;
	if(r->ifcall.newfid != r->ifcall.fid && (nfid = newfid(r->ifcall.newfid, &e)) == nil){
		respond(r, e);
		return;
	}

	path = estrdup(fid->path);
	e = nil;
	for(i=0; i<r->ifcall.nwname; i++)
		if(userwalk(fid->u, &path, r->ifcall.wname[i], &r->ofcall.wqid[i], &e) < 0)
			break;

	if(i == r->ifcall.nwname){		/* successful clone or walk */
		r->ofcall.nwqid = i;
		if(nfid){
			nfid->path = path;
			nfid->u = fid->u;
		}else{
			free(fid->path);
			fid->path = path;
		}
	}else{
		if(i > 0)		/* partial walk? */
			r->ofcall.nwqid = i;
		else
			respond(r, e);

		if(nfid)		/* clone implicit new fid */
			freefid(nfid);
		free(path);
	}
	respond(r, nil);
}


Srv fs=
{
.attach=	rattach,
.auth=	rauth,
.open=	ropen,
.create=	rcreate,
.read=	fsread,
.write=	rwrite,
.remove=	rremove,
.stat=	rstat,
.wstat=	rwstat,
.walk=	rwalk,
};

void
threadmain(int argc, char **argv)
{
	char *service;
	int port;

	time0 = time(0);
	service = nil;
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		service = EARGF(usage());
		break;
	case 'p':
		port = atol(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		dir = argv[0];
	else
		dir = "/";

	threadpostmountsrv(&fs, service, nil, MREPL|MCREATE);
	threadexits(nil);
}
