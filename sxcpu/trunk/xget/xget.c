#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <regex.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <zlib.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"

#include "xcpu.h"
#include "xget.h"

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Spdirops dir_ops = {
	.first = dir_first,
	.next = dir_next,
	.destroy = dir_destroy,
};

Spfileops data_ops = {
	.read = data_read,
};

Spfileops redir_ops = {
	.read = redir_read,
};

Spfileops avail_ops = {
//	.read = avail_read,
	.write = avail_write,
       	.wstat = avail_wstat,
	.closefid = avail_closefid,
};

Spfileops log_ops = {
	.read = log_read,
	.write = log_write,
};

Spfileops checksum_ops = {
	.read = checksum_read,
};

Spcfsys *masterfs;
Spcfid *logfid;
Spsrv *srv;
Spfile *root;
Spuser *user;
Spgroup *group;
Spuserpool *up;
int port = XGET_PORT;
unsigned int debuglevel = 0;
char *netaddress = 0;
char *xget_ename;
int xget_ecode;
int numconnects = 0;
int numtransfers = 0;
int maxlevels = 4;
int maxconnections = 10;
int maxretries = 15; /* give at least 15 attempts by default */
int retrytime = 10; /* wait default of 20 seconds before trying again */
int singlefile = 0; /* Flag that indicates whether we are serving a single file */
int changeperms = 1;
int rootonly = 0;
int skipmnt = 0;
char *path, *outname;
int ticksig;
u64 qpath = 1;
File *files;
Client *clients;
Server *servers;
time_t starttime;
time_t endtime;
time_t servicetime;
dev_t fs_dev;

static int netread(Spfile *parent, char *lprefix, char *nprefix, char *name);
int netpathread(char *fpath);
static int fullfilename(Spfile *file, char* fullname, int buflen);
static void netreadcb(Spcfd *fd, void *a);


static void 
usage(char *name) {
	fprintf(stderr, "usage as the master server: %s [-D debuglevel] [-p port] [-o] [-x] [-m] %s", 
		name, " file|directory\n");
	fprintf(stderr, "usage as a client         : %s [-D debuglevel] [-p port] %s %s", 
		name, "<-n netaddr> [-s] [-o] <src file | src dir | .> ", 
		"[src file | src dir] ... dest\n");
	exit(1);
}

time_t now(void) {
	struct timeval t;
	gettimeofday(&t, 0);
	return t.tv_sec;
}

time_t future(time_t delta) {
	return now() + delta;
}

void removeworker(File *f, Worker *worker) {
	Worker *cur;
	Client *client;
	Usedworker *uw, *uw2;
	File *f2;

	if(!worker)
		return;

	/* In the case where a pointer was not set to null, this will ensure
	   that we do not try to free a dangling pointer
	*/
	for(cur = f->firstworker; cur; cur = cur->next) {
		if(cur == worker)
			break; 	/* Continue with delete */
	}
	if(!cur)
		return; /* Not found in queue, already removed */

	for (client = clients; client != NULL; client = client->next) {
		for (uw = client->workersused; uw != NULL; ) {
			uw2 = uw->next;
			if (uw->worker == worker) {
				if (uw->next)
					uw->next->prev = uw->prev;
				if (uw->prev)
					uw->prev->next = uw->next;
				if (uw == client->workersused)
					client->workersused = uw->next;

				if (uw->worker->server && 
				    uw->worker->server->conns > 0)
					uw->worker->server->conns--;
				
				free(uw);
			}
			uw = uw2;
		}
	}

	debug(Dbgsrvfn, "worker %p is done\n", worker);
	if (worker == f->firstworker)
		f->firstworker = worker->next;

	if (worker == f->lastworker)
		f->lastworker = worker->prev;

	if (worker == f->nextworker) {
		if (worker->next) 
			f->nextworker = worker->next;
		else
			f->nextworker = f->firstworker;
	}

	if (worker->next)
		worker->next->prev = worker->prev;
	if (worker->prev)
		worker->prev->next = worker->next;

	f->numworkers--;
	//remove old servers that are no longer serving from the list

	if (worker->server &&  worker->server->conns == 0) {
		if (worker->server->next)
			worker->server->next->prev = worker->server->prev;
		if (worker->server->prev)
			worker->server->prev->next = worker->server->next;
		if (worker->server == servers)
			servers = worker->server->next;
		for (f2 = files; f2 != NULL; f2 = f2->next) {
			for(cur = f2->firstworker; cur; cur = cur->next) {
				if (cur == worker)
					continue;
				else if (cur->server == worker->server)
					cur->server = NULL;
			}
		}
		
		free(worker->server);
	}
	
	free(worker);
}

static int
xget_haserror() {
	int ret;
	
	ret = 0;
	if (xget_ename)
		ret = 1;

	return ret;
}

static void
xget_uerror(int ecode) {
	char *ename;

	ename = strerror(ecode);
	xget_ename = sp_malloc(strlen(ename) + 1);
	sprintf(xget_ename, "%s", ename);
	xget_ecode = ecode;
}

static void
debug(int level, char *fmt, ...)
{
	va_list arg;
	char buf[512];

	if (!(debuglevel & level))
		return;
	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);

	if (!netaddress)
		fprintf(stderr, "Master: %s", buf);
	else
		fprintf(stderr, "%s", buf);

	if (logfid)
		spc_write(logfid, (u8 *) buf, strlen(buf), 0);
}

static void 
reload(int sig) {
	debug(Dbgfn, "Handling HUP!  Reloading...\n");
	/* Should kill connections? */

	/* stat file for changes- TODO */
        /* localfileread(file);     */
}

static void 
sigalrm(int sig)
{
	File *f;
	
	alarm(1);
	ticksig = 1;
	if (servicetime && time(NULL) > servicetime) {
		for(f = files; f != NULL; f = f->next) {
			if (f->availfid)
				spc_close(f->availfid);
		}
	
		if (masterfs) {
			spc_umount(masterfs);
			masterfs = NULL;
		}
		
		free(path);
		exit(0);
	}
}

//Taken from xcpufs/ufs.c
static u32
umode2npmode(mode_t umode)
{
	u32 ret;

	ret = umode & 0777;
	if (S_ISDIR(umode))
		ret |= Dmdir;

	if (S_ISLNK(umode))
		ret |= Dmsymlink;
	if (S_ISSOCK(umode))
		ret |= Dmsocket;
	if (S_ISFIFO(umode))
		ret |= Dmnamedpipe;
	if (S_ISBLK(umode))
		ret |= Dmdevice;
	if (S_ISCHR(umode))
		ret |= Dmdevice;
	if (umode & S_ISUID)
		ret |= Dmsetuid;
	if (umode & S_ISGID)
		ret |= Dmsetgid;

	return ret;
}

//Taken from xcpufs/ufs.c
static mode_t
np2umode(u32 mode, char *extension)
{
	mode_t ret;

	ret = mode & 0777;
	if (mode & Dmdir)
		ret |= S_IFDIR;

	if (mode & Dmsymlink)
		ret |= S_IFLNK;
	if (mode & Dmsocket)
		ret |= S_IFSOCK;
	if (mode & Dmnamedpipe)
		ret |= S_IFIFO;
	if (mode & Dmdevice) {
		if (extension && extension[0] == 'c')
			ret |= S_IFCHR;
		else
			ret |= S_IFBLK;
	}
	
	if (!(ret&~0777))
		ret |= S_IFREG;

	if (mode & Dmsetuid)
		ret |= S_ISUID;
	if (mode & Dmsetgid)
		ret |= S_ISGID;

	return ret;
}

static int
fileretry(File *f, u64 offset)
{
	char *tname;

	debug(Dbgfn, "Retrying: %s for the %d time\n", f->lname, f->retries);
	tname = sp_malloc(strlen(f->nname) + 6);
	if (!tname)
		goto error;

	sprintf(tname, "%s%s", f->nname, "/data");
	if (f->retries == maxretries ) {
		sp_werror("Too many retries for file: %s", EIO, f->lname);
		goto error;
	} 

	f->retries++;		
	if ((file_finalize(f, 1)) < 0)
		goto error;
	
	if (f->datafd) {
		spcfd_remove(f->datafd);
		f->datafd = NULL;
	}

	f->checksum = adler32(0L, Z_NULL, 0);
	f->checksum_ptr = 0;
	f->progress = time(NULL);
	f->finished = 0;
	if (offset == 0)
		f->datalen = 0;

	f->datafid = spc_open(masterfs, tname, Oread);
	if (!f->datafid)
		goto error;

	f->datafd = spcfd_add(f->datafid, netreadcb, f, offset);
	free(tname);
	return 0;
	
error:
	if (tname)
		free(tname);

	return -1;
}

static int
file_finalize(File *f, int write)
{
	mode_t umode;
	char *ename;
	int ecode;
		
	if (!f)
		return 0;
	
	if (f->datafid) {
		spc_close(f->datafid);
		f->datafid = NULL;
	}

	if (f->fs && f->fs != masterfs)
		spc_umount(f->fs);
	
	f->fs = masterfs;
	if (!changeperms) {
		f->finished = 2;
		goto done;
	}

	if (!write) {
		umode = np2umode(f->datafile->mode, NULL);
		if ((chmod(f->lname, umode)) == -1) {
			sp_uerror(errno);
			if (sp_haserror()) {
				sp_rerror(&ename, &ecode);
				debug(Dbgclntfn, "Could not change permissions" 
				      " of file: %s.  Error: %s\n", f->lname, ename);
				sp_werror(NULL, 0);
			}
		}
		
		if (geteuid() == 0 && 
		    (chown(f->lname, f->datafile->uid->uid, -1)) == -1) { 
			sp_uerror(errno);
			if (sp_haserror()) {
				sp_rerror(&ename, &ecode);
				debug(Dbgclntfn, "Could not change user ownership" 
				      " of file: %s.  Error: %s\n", f->lname, ename);
				sp_werror(NULL, 0);
			}
		}
		
		if ((chown(f->lname, -1, f->datafile->gid->gid)) == -1) {
			sp_uerror(errno);
			if (sp_haserror()) {
				sp_rerror(&ename, &ecode);
				debug(Dbgclntfn, "Could not change group ownership" 
				      " of file: %s to gid: %u.  Error: %s\n", f->lname, 
				      f->datafile->gid->gid, ename);
				sp_werror(NULL, 0);
			}
		}
		
		f->finished = 2;
	}	
	else {
		if ((chmod(f->lname, 0600)) == -1) {
			sp_uerror(errno);
			if (sp_haserror()) {
				sp_rerror(&ename, &ecode);
				debug(Dbgclntfn, "Could not change permissions" 
				      " of file: %s.  Error: %s\n", f->lname, ename);
				sp_werror(NULL, 0);
			}
			return -1;
		}
	}

done:
	return 0;
}

static int
tick(void)
{
	int fdone, n;
	File *f;

	if (!endtime) {
		fdone = 1;
		for(f = files; f != NULL; f = f->next) {
//			debug(Dbgfn, "tick\n");
			if (f->finished < 0) {
				debug(Dbgclntfn, "File: %s checksum did not match, retrying\n",
				      f->nname);
				if (fileretry(f, 0) < 0)
					return -1;
			}

			if (f->finished == 1 && (f->finished = matchsum(f)) == 1)
				file_finalize(f, 0);
			
			if (f->finished == 2)
				continue;

			if (f->finished == 4) {
				debug(Dbgfn, "Error downloading file %s\n", f->lname);
/*				if (f->datafid) {
					spc_close(f->datafid);
					f->datafid = NULL;
				}
			
				if (f->fs && f->fs != masterfs)
					spc_umount(f->fs);		       
*/			
				f->finished = 2;
			}
			
			fdone = 0;
			if (time(NULL) - f->progress > retrytime) {
				debug(Dbgclntfn, "File: %s timed out, retrying\n", f->nname);
				if (fileretry(f, f->datalen) < 0)
					return -1;
			}
		}	
	
		if (fdone) {
			endtime = time(NULL);
			debug(Dbgfn, "finished download for %d seconds\n",
			      (endtime - starttime));
			if (numconnects == 0)
				servicetime = endtime;
			else {
				if (n < 5)
					n = 5;
				
				n = (endtime - starttime);
				servicetime = endtime + n * 2;
			}
		}
	}
	
	else {
		if (numconnects == 0)
			servicetime = time(NULL);
	}
	
	return 0;
}

static void
dir_remove(Spfile *dir)
{
	Spfile *f, *f2, *parent;
	
	for(f = dir->dirfirst; f != NULL; ) {
		f2 = f->next;
		parent = f->parent;
		if (f->mode & Dmdir)
			dir_remove(f);
		else
			spfile_decref(f);

		if (parent)
			spfile_decref(parent);
		
		f = f2;
	}
	
	spfile_decref(dir);
}

static void
parent_remove(Spfile *dir)
{
	if (dir->parent->dirfirst == dir)
		dir->parent->dirfirst = dir->next;
	if (dir->parent->dirlast == dir)
		dir->parent->dirlast = dir->prev;
	
	if (dir->next) 
		dir->next->prev = dir->prev;
	if (dir->prev) 
		dir->prev->next = dir->next;

	spfile_decref(dir->parent);	
	dir->parent = NULL;
}

static int
fullfilename(Spfile *file, char* fullname, int buflen) 
{
	int len, bufleft;
	char *tbuf;
	Spfile *f;
	
	if(!file)
		return 0;
	
	bufleft = buflen;
	tbuf = (char *) malloc(bufleft);
	for (f = file; f != NULL; f = f->parent) {
		len = 0;
		if (!strlen(f->name))
		  break;

		snprintf(tbuf, buflen, "/%s", f->name);
		len = strlen(tbuf);
		if (strlen(fullname))
			snprintf(tbuf + len, buflen - len, "%s", fullname);

		snprintf(fullname, buflen, "%s", tbuf);
		bufleft -= len;
	}

	free(tbuf);
	return(buflen - bufleft);
}

static Spfile*
updatedir(Spfile *dir, char *name)
{
	int blen, bsize, len, ecode;
	char *buf, *ename;
	struct stat st;
	Spfile *flist, *f, *f1;
	DIR *d;
	struct dirent *de;

	bsize = NAME_MAX + 1;
	blen = strlen(name);
	buf = sp_malloc(bsize);
	if (!buf)
		goto error;
	strcpy(buf, name);

	/* Capture 9P directory entries.  Anything not removed from
	 * local directory entry matches will need to be pruned from
	 * the 9P directory.
	 */
	flist = dir->dirfirst;
	dir->dirfirst = NULL;
	dir->dirlast = NULL;
	/* Keeping /log */
	if ((strlen(dir->name) == 0) && flist && 
	    !(flist->mode & Dmdir) && !strcmp(flist->name, "log")) {
		f = flist;
		flist = f->next;
		f->next = NULL;
		dir->dirfirst = f;
		dir->dirlast = f;
	}

	/* Removing all 9p files in flist */
	f = flist;
	while (f != NULL) {
		f1 = f->next;
		debug(Dbgfn, "Removing %s\n", f->name);
		parent_remove(f);
		dir_remove(f);
		f = f1;
	}

	d = opendir(name);
	if (!d) {
		sp_uerror(errno);
		goto error;
	}

	while ((de = readdir(d))) {
		/* ignore "hidden" files */
		if (*de->d_name == '.')
			continue;

		len = strlen(de->d_name);
		if (blen+len+1 > bsize) 
			continue;
				
		snprintf(buf+blen, bsize-blen, "/%s", de->d_name);
		if (lstat(buf, &st) < 0) {
			sp_uerror(errno);
			goto error;
		}

		if (skipmnt && (st.st_dev != fs_dev))
			goto error;

		/* skip files other than regular files */
		if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
			continue;

		debug(Dbgfn, "Adding file: %s\n", buf);
		localfileread(dir, buf);
		if (sp_haserror())
			goto error;
	}
	closedir(d);
	free(buf);

	return dir;

error:
	if (sp_haserror()) {
		sp_rerror(&ename, &ecode);
		debug(Dbgfn, "Error while scanning files for changes: %s\n", ename);
	} else
		debug(Dbgfn, "Error: Skipping %s\n", buf);

	if (buf)
		free(buf);
	if (d)
		closedir(d);

	return NULL;
}

static Spfile*
dir_update(Spfile *dir)
{
	char* name;
	Spfile *ret, *par;
	struct stat st;
	int namelen, namesize;
	
	ret = dir;
	namelen = 0;
	namesize = NAME_MAX + 1;
	name = (char*) calloc(namesize, 1);
	strncpy(name, path, namesize);
	namelen = strlen(name);
	
	if(strlen(dir->name) && !singlefile) {
		if( (namelen += fullfilename(dir, name + namelen, namesize - namelen)) <= 0) {
			debug(Dbgfn, "Failed to find full 9p file name for %s\n", dir->name);
			goto error;
		}
	}
	else if(!strlen(dir->name) && singlefile)
		goto done;

	if (stat(name, &st) < 0) {
		sp_werror(Enotfound, ENOENT);
		goto error;
	}

	if (st.st_mtime == dir->mtime)
		goto done;
       
	if (S_ISREG(st.st_mode)) {
		par = dir->parent;
		parent_remove(dir);
		dir_remove(dir);
		ret = localfileread(par, name);
		if (sp_haserror())
			goto error;
	} 
	else if (S_ISDIR(st.st_mode))
		ret = updatedir(dir, name);
	else {
		sp_werror(Eperm, EPERM);
		ret = NULL;
	}

	if (ret)
		dir->mtime = st.st_mtime;

done:
	free(name);
	return ret;

error:
	free(name);
	return NULL;
}
		
static Spfile*
dir_first(Spfile *dir)
{
	
	if (!netaddress) {
		dir = dir_update(dir);
		if (!dir)
			return NULL;
	}
       
	spfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Spfile*
dir_next(Spfile *dir, Spfile *prevchild)
{
	spfile_incref(prevchild->next);
	return prevchild->next;
}

static void
dir_destroy(Spfile *dir) {
	File *f, *f2;
	Spfid *fid, **pool;
	Worker *w;
	Spfile *file;
	Spfilefid *ffid;
	Spconn *conns;
	int i;
	Req *req, *req2;

	f = (File *) dir->aux;
	if (f) {
		if (f == files) {
			files = f->next;
			if (files)
				files->prev = NULL;
		}
		if (f->next)
			f->next->prev = f->prev;
		if (f->prev)
			f->prev->next = f->next;
		
		for (w = f->firstworker; w != NULL; )
			removeworker(f, w);
	       		
		for (req = f->reqs; req != NULL; ) {
			req2 = req->next;
			free(req);
			req = req2;
		}
		
		if (!netaddress) {
			for (conns = srv->conns; conns != NULL; conns = conns->next) {
				pool = conns->fidpool;
				for (i = 0; i < FID_HTABLE_SIZE; i++) {
					fid = pool[i];
					if (fid) {
						ffid = fid->aux;
						file = ffid->file;
						f2 = file->aux;
						if (f == f2)
							file->aux = NULL;
					}
				}
			}
		}

		free(f);
		dir->aux = NULL;
	}	
}

static Spfile *
create_file(Spfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	    Spuser *usr, Spgroup *grp, void *aux)
{
	Spfile *ret;
	char *sname;

	if (name && (sname = strrchr(name, '/'))) 
		sname++;
	else 
		sname = name;
		
	if ((ret = check_existance(parent, sname)))
		return ret;

			
	ret = spfile_alloc(parent, sname, mode, qpath, ops, aux);
	if (!ret)
		return NULL;
	
	if (parent) {
		if (parent->dirlast) {
			parent->dirlast->next = ret;
			ret->prev = parent->dirlast;
		} else
			parent->dirfirst = ret;

		parent->dirlast = ret;
		if (!usr)
			usr = parent->uid;
	}

	if (!usr)
		usr = user;
	
	ret->uid = ret->muid = usr;
	if (!grp)
		grp = group;

	ret->gid = grp;
	
	//	ret->atime = ret->mtime = time(NULL);
	ret->atime = ret->mtime = 0;
	spfile_incref(ret);
	
	return ret;
}

static void
fsinit(void)    
{      
	root = spfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	spfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;
	create_file(root, "log", 0222, Qlog, &log_ops, NULL, NULL, NULL);
}

static File *
filealloc(Spfile *parent, char *nname, char *lname, u64 datasize, u64 datalen, 
	  u32 mtime, u32 mode, Spuser *usr, Spgroup *grp)
{
	int qp;
	File *f;
	Spfile *file;

	f = sp_malloc(sizeof(*f) + strlen(nname) + strlen(lname) + 2);
	if (!f)
		return NULL;
	qp = qpath++ * 16;
	f->nname = ((char *) f) + sizeof(*f);
	strcpy(f->nname, nname);
	f->lname = ((char *) f) + sizeof(*f) + strlen(nname) + 1;
	strcpy(f->lname, lname);
	f->datasize = datasize;
	f->datalen = datalen;
	f->numworkers = 0;
	f->fs = NULL;
	f->firstworker = NULL;
	f->lastworker = NULL;
	f->nextworker = NULL;
	f->reqs = NULL;
	f->datafid = NULL;
	f->availfid = NULL;
	f->datafd = NULL;
	f->checksum = adler32(0L, Z_NULL, 0);
	f->checksum_ptr = 0;
	f->finished = 0;
	f->progress = time(NULL);
	f->retries = 0;
	f->dir = create_file(parent, nname, 0555 | Dmdir, (qp << 8), &dir_ops, usr, grp, f);
	if (check_existance(f->dir, "data")) {
		free(f);
		return NULL;
	}

	f->dir->mtime = f->dir->atime = mtime;
	f->datafile = create_file(f->dir, "data", mode, (qp << 8) | Qdata, &data_ops, usr, grp, f);
	f->datafile->length = datasize;
	create_file(f->dir, "avail", 0666, (qp<<8) | Qavail, &avail_ops, NULL, NULL, f);
	file = create_file(f->dir, "redir", 0444, (qp << 8) | Qredir, &redir_ops, NULL, NULL, f);
	file->length = 32;
	
	create_file(f->dir, "checksum", 0444, (qp << 8) | Qchecksum, &checksum_ops, NULL, NULL, f);
	f->next = files;
	f->prev = NULL;
	if (files)
		files->prev = f;
	files = f;

	return f;
}

static Spfile*
localfileread(Spfile *parent, char *filename)
{
	char *name, *nextf, *ename;
	struct stat st;
	File *f;
	Spfile *dir, *ret;
	DIR *dirstr;
	struct dirent *de;
	int ecode;
	u32 checksum, npmode;
	Spuser *usr;
	Spgroup *grp;

	f = NULL;
	checksum = 0;
	ret = NULL;
	usr = NULL;
	grp = NULL;
	nextf = NULL;

	if (lstat(filename, &st) < 0) {
		sp_uerror(errno);
		goto error;
	}

	if (skipmnt && (st.st_dev != fs_dev))
		goto error;

	if ((name = strstr(filename, path)) != NULL) 
		name = filename + strlen(path);
	else
		name = filename;

	npmode = umode2npmode(st.st_mode);
	if (!rootonly) {
		usr = sp_unix_users->uid2user(sp_unix_users, st.st_uid);
		grp = sp_unix_users->gid2group(sp_unix_users, st.st_gid);
	}

	if (S_ISREG(st.st_mode)) {
		f = filealloc(parent, name, filename, st.st_size, st.st_size,  
			      st.st_mtime, npmode, usr, grp);
		if (!f)
			goto error;

		debug(Dbgsrvfn, "Added file: %s\n", f->dir->name);
		ret = f->dir;
	
	} else if (S_ISDIR(st.st_mode)) {
		if (strcmp(path, filename)) {
			dir = create_file(parent, name, npmode, 
					  qpath++ * 16, &dir_ops, usr, grp, NULL);
			if (!dir) 
				goto error;
			
			debug(Dbgsrvfn, "Added dir: %s\n", dir->name);
		}
		else {
			dir = root;
			dir->mode = npmode;
		}

		ret = dir;
		dirstr = opendir(filename);
		if (dirstr) {
			while ((de = readdir(dirstr))) {
				if (!de)
					goto error;

				if(strncmp(de->d_name, ".", 1) == 0)
					continue;
				
				nextf = (char *) sp_malloc(strlen(filename) + 
							   strlen(de->d_name) + 2);
				
				if (!nextf)
					goto error;

				sprintf(nextf, "%s/%s", filename, de->d_name);
				localfileread(dir, nextf);
				
				free(nextf);
				nextf = NULL;
			}
		
			closedir(dirstr);
		} else
			goto error;

		ret->mtime = ret->atime = st.st_mtime;	
	} else
		goto error;

	return ret;

error:
	if (sp_haserror()) {
		sp_rerror(&ename, &ecode);
		debug(Dbgfn, "Error: Skipping %s : %s\n", 
		      filename, ename);
		sp_werror(NULL, 0);
	} else
		debug(Dbgfn, "Error: Skipping %s\n", filename);

	if (nextf)
		free(nextf);
	
	return NULL;
}

static Spfile *
check_existance(Spfile *parent, char *name) {
	Spfile *p, *dir;

	dir = NULL;
	for (p = parent->dirfirst; p != NULL; p = p->next) {
		if (!strcmp(p->name, name)) {
			dir = p;
			break;
		}
	}

	return dir;
}
	
	
	
static void
respondreqs(File *f)
{
	int fd, n;
	Req *req, *nreq;
	Spfcall *rc;
	u8 *buf;
	u64 count;

	req = f->reqs;
	fd = -1;
	buf = NULL;
	while (req != NULL) {
		count = f->datalen - req->offset;
		if (count < 0)
			count = 0;

		if (count > req->count)
			count = req->count;
		
		if (f->datalen == f->datasize || (count>4096 && req->offset + count < f->datalen)) {
			/* if we haven't got the whole file and can send back
			   only small chunk, don't respond, wait for more */
			
			buf = sp_malloc(count);
			if (!buf)
				goto error;

			if ((fd = open(f->lname, O_RDONLY)) == -1)
				goto error;
				
			if ((lseek(fd, req->offset, SEEK_SET)) == (off_t)-1) 
				goto error;
			
			if ((n = read(fd, buf, count)) < 0)
				goto error;


			if (req->offset == f->checksum_ptr) {
				f->checksum = adler32(f->checksum, (const Bytef *) buf, n);
				f->checksum_ptr += n;
			}

			close(fd);
			if (n < count)
				count = n;

			rc = sp_create_rread(count, buf);
			free(buf);
			sp_respond(req->req, rc);
			nreq = req->next;
			if (req->prev)
				req->prev->next = req->next;
			else
				f->reqs = req->next;

			if (req->next)
				req->next->prev = req->prev;
			free(req);
			req = nreq;
		} else
			req = req->next;
	}

	return;

error:
	debug(Dbgfn, "Error: Cannot process request for file %s : %s\n", 
	      f->lname, strerror(errno));

	if (buf)
		free(buf);
	if (fd > 0)
		close(fd);
	rc = sp_create_rflush();
	sp_respond(req->req, rc);
	nreq = req->next;
	if (req->prev)
		req->prev->next = req->next;
	else
		f->reqs = req->next;
	
	if (req->next)
		req->next->prev = req->prev;
	free(req);
}

static void
netreadcb(Spcfd *fd, void *a)
{
	int n, lfd, readsize;
	File *f;
	u8 *buf;
	struct stat *st;
	
	f = a;
	st = NULL;
	buf = NULL;

	if (f->datalen == 0) {
		st = sp_malloc(sizeof(struct stat));
		if (!(lstat(f->lname, st))) {
			if (S_ISREG(st->st_mode)) {
				if ((unlink(f->lname)) < 0) {
					xget_uerror(errno);
					goto error;
				}
			}
				
			else if (S_ISDIR(st->st_mode)) {
				if ((rmdir(f->lname)) < 0) {
					xget_uerror(errno);
					goto error;
				}
			}
		}
		free(st);
		st = NULL;
	}

	readsize = fd->iounit;
	buf = sp_malloc(readsize);
	n = spcfd_read(fd, buf, readsize);
	if (n < 0)
		goto error;
		
	if ((lfd = open(f->lname, O_CREAT | O_RDWR, 0600)) == -1) {
		xget_uerror(errno);
		goto error;
	}

	if ((lseek(lfd, f->datalen, SEEK_SET)) == (off_t)-1) {
		xget_uerror(errno);
		goto error;
	}
		
	if ((write(lfd, buf, n)) != n) {
		xget_uerror(errno);
		goto error;
	}

	if (f->datalen == f->checksum_ptr) {
		f->checksum = adler32(f->checksum, (const Bytef *) buf, n);
		f->checksum_ptr += n;
	}	

	f->datalen += n;
	if (f->datalen >= f->datasize) {
		spcfd_remove(fd);
		f->datafd = NULL;
		if ((lseek(lfd, 0, SEEK_SET)) == (off_t)-1) {
			xget_uerror(errno);
			goto error;
		}

		f->finished = 1;
	}

	f->progress = time(NULL);
	free(buf);
	close(lfd);
	respondreqs(f);
	return;
	
error:
	f->finished = 4;

	if (st)
		free(st);
	if (buf)
		free(buf);
	if (lfd > -1)
		close(lfd);

	return;
}

static int
matchsum(File *f)
{
	u32 checksum;
	Spcfid *checksumfid;
	char *buf = NULL;
	int n, blen;
	
	blen = strlen(f->nname) + 16;
	if (blen < 128)
		blen = 128;
	
	buf = sp_malloc(blen);
	if (!buf)
		goto error;
	
	sprintf(buf, "%s/checksum", f->nname);
	checksumfid = spc_open(masterfs, buf, Oread);
	if (!checksumfid)
		goto error;
	
	n = spc_read(checksumfid, (u8 *) buf, blen, 0);
	if (n < 0)
		goto error;
	
	spc_close(checksumfid);
	buf[n] = '\0';
	checksum = strtoul(buf, NULL, 0);
	free(buf);
	
	if (f->checksum == checksum)
		return 1;
	else
		return -1;
error:
	if (buf)
		free(buf);
	
	return -1;
}

static int
netfileread(Spfile *dir, char *lname, char *nname, u64 len, int mtime, 
	    u32 npmode, Spuser *usr, Spgroup *grp)
{
	int n, blen;
	char *buf, *redirto;
	Spcfid *datafid, *availfid, *redirfid;
	Spcfsys *redirfs;
	File *file;
	
	datafid = NULL;
	availfid = NULL;
	redirfid = NULL;
	redirfs = NULL;

	blen = strlen(nname) + 16;
	if (blen < 128)
		blen = 128;
	buf = sp_malloc(blen);
	if (!buf)
		return -1;

	file = filealloc(dir, nname, lname, len, 0, mtime, npmode, 
			 usr, grp);
	if (!file) {
		if (sp_haserror())
			goto error;
		
		return 0;
	}

	file->fs = NULL;
	file->datafid = NULL;
	file->datafd = NULL;
	file->availfid = NULL;
	
	/* check if we should go somewhere else for the file */
	sprintf(buf, "%s/redir", nname);
	redirfid = spc_open(masterfs, buf, Oread);
	if (!redirfid)
		goto error;

	n = spc_read(redirfid, (u8 *) buf, blen, 0);
	if (n < 0)
		goto error;

	buf[n] = '\0';
	spc_close(redirfid);
	redirto = sp_malloc(strlen(buf) + 1);
	strcpy(redirto, buf);
	if (!strcmp(buf, "me")) {
		redirfs = masterfs;
		debug(Dbgclntfn, "Downloading file: %s from master\n", nname);
	}
	else {
		debug(Dbgclntfn, "Redirected to %s for file: %s\n", buf, nname);
		redirfs = spc_netmount(buf, user, port, NULL, NULL);
		if (!redirfs)
			redirfs = masterfs;
	}

	sprintf(buf, "%s/data", nname);
	datafid = spc_open(redirfs, buf, Oread);
	if (!datafid)
		goto error;

	sprintf(buf, "%s/avail", nname);
	availfid = spc_open(masterfs, buf, Owrite);
	if (!availfid)
		goto error;

	snprintf(buf, blen, "%d %s", port, redirto);
	n = spc_write(availfid, (u8 *) buf, strlen(buf) + 1, 0);
	if (n < 0)
		goto error;
	
	file->fs = redirfs;
	file->datafid = datafid;
	file->datafd = spcfd_add(file->datafid, netreadcb, file, 0);
	file->availfid = availfid;
	free(buf);
	free(redirto);
	return 0;

error:
	if (file)
		file->finished = 4;	
	/*	if (datafid)
		spc_close(datafid);

	if (availfid)
		spc_close(availfid);

	if (redirfs && redirfs != masterfs) {
		spc_umount(redirfs);
		file->fs = NULL;
	}
	*/
	free(buf);
	return 0;
}

static int
netdirread(Spfile *parent, char *lname, char *nname, u32 npmode, Spuser *usr,
	   Spgroup *grp)
{
	int ret, i, n, r;
	struct stat ustat;
	Spfile *dir;
	Spcfid *fid;
	Spwstat *st;
	char **fnames;
	struct timeval tv;
	
	fid = spc_open(masterfs, nname, Oread);
	if (!fid)
		return -1;

	if (stat(lname, &ustat) < 0) {
		if (mkdir(lname, 0700) < 0) {
			sp_uerror(errno);
			ret = -1;
			goto done;
		}

		stat(lname, &ustat);
	}

	if (!S_ISDIR(ustat.st_mode)) {
		sp_werror(Enotdir, ENOTDIR);
		ret = -1;
		goto done;
	}

	if (strlen(nname) > 0)
		dir = create_file(parent, nname, npmode, qpath++ * 16, &dir_ops,
				  usr, grp, NULL);
	else {
		dir = parent;
		if (usr)
			dir->uid = usr;
		if (grp)
			dir->gid = grp;
	}
		

	if (!dir) {
		ret = -1;
		goto done;
	}

	gettimeofday(&tv, NULL);
	srand(tv.tv_sec);
	while ((n = spc_dirread(fid, &st)) > 0) {
		fnames = (char **) sp_malloc(sizeof(char *) * n);
		for (i=0; i < n; i++) {
			fnames[i] = NULL;
		}

		for(i = 0; i < n; i++) {
			if (dir == root && !strcmp(st[i].name, "log"))
				continue;
			
			
			r = (int) (n * (rand() / (RAND_MAX + 1.0)));
			while (fnames[r] != NULL) {
				r =(int) (n * (rand() / (RAND_MAX + 1.0)));
			}

			fnames[r] = sp_malloc(strlen(st[i].name) + 1);
			snprintf(fnames[r], strlen(st[i].name) + 1, "%s", st[i].name);
		}

		for (i=0; i < n; i++) {
			if (fnames[i] == NULL)
				continue;

			debug(Dbgclntfn, "Setting up file: %s\n", fnames[i]);			
			if (netread(dir, lname, nname, fnames[i]) < 0) {
				ret = -1;
				continue;
			}

			free(fnames[i]);
			fnames[i] = NULL;
		}

		free(fnames);
       		free(st);
	}

	if (n < 0)
		ret = -1;
	else {
		ret = 0;
		free(st);
	}

done:
	spc_close(fid);
	for (i=0; i < n; i++) {
		if (fnames[i])
			free(fnames[i]);
	}
	return ret;
}

static int
netread(Spfile *parent, char *lprefix, char *nprefix, char *name)
{
	int i, mtime, ret, ecode;
	char *nname, *lname, *s, *ename;
	Spwstat *st;
	u32 npmode;
	Spuser *usr;
	Spgroup *grp;
	u64 len;

	usr = NULL;
	grp = NULL;
	nname = sp_malloc(strlen(nprefix) + strlen(name) + 16);
	sprintf(nname, "%s/%s", nprefix, name);

	lname = sp_malloc(strlen(lprefix) + strlen(name) + 2);
	sprintf(lname, "%s/%s", lprefix, name);

	/* get rid of trailing '/' */
	for(i = strlen(nname) - 1; i >= 0 && nname[i] == '/'; i--)
		;
	nname[i+1] = '\0';

	for(i = strlen(lname) - 1; i >= 0 && lname[i] == '/'; i--)
		;
	lname[i+1] = '\0';

	s = nname + strlen(nname);
	strcat(nname, "/data");
	st = spc_stat(masterfs, nname);
	*s = '\0';
	if (!st || st->mode & Dmdir) {
		/* if there is no "data" file, it could be a directory
		   
		   if there is a data file and it is a directory, read the 
		   directory 
		*/
		if (st)
			free(st);

		sp_werror(NULL, 0);
		st = spc_stat(masterfs, nname);
		if (!st) {
			sp_werror("Could not find file: %s", ENOENT, nname);
			goto error;
		}
		if (!(st->mode & Dmdir)) {
			sp_werror(Enotdir, ENOTDIR);
			goto error;
		}

		npmode = st->mode;
		if (!rootonly) {
			usr = sp_unix_users->uname2user(sp_unix_users, st->uid);
			grp = sp_unix_users->gname2group(sp_unix_users, st->gid);
		}

		ret = netdirread(parent, lname, nname, npmode, usr, grp);
	} else {
		len = st->length;
		mtime = st->mtime;
		npmode = st->mode;
		if (!rootonly) {
			usr = sp_unix_users->uname2user(sp_unix_users, st->uid);
			grp = sp_unix_users->gname2group(sp_unix_users, st->gid);
		}
	
		/* regular file, set it up and retry up to maxretries */
		for (i=0; i < maxretries; i++) {
			sp_werror(NULL, 0);

			ret = netfileread(parent, lname, nname, len, mtime, 
					  npmode, usr, grp);
			if (!ret)
				break;
		}
	}
	
       	if (ret == -1 || sp_haserror()) {
		sp_rerror(&ename, &ecode);
		debug(Dbgfn, "Fatal error while setting up file %s: %s\n", nname, ename);
		sp_werror(NULL, 0);
		
	}
	
	free(st);
	free(nname);
	free(lname);
	return ret;

error:
	if (usr)
		free(usr);
	if (grp)
		free(grp);
	free(nname);
	free(lname);
	if (st)
	  free(st);
	return -1;
}

int
netpathread(char *fpath)
{
	int i;
	char *s, *p, *nprefix;
	Spfile *dir, *t;

	path = sp_malloc(strlen(fpath) + 1);
	strcpy(path, fpath);

	/* get rid of trailing '/' */
	for(i = strlen(path) - 1; i >= 0 && path[i] == '/'; i--)
		;
	path[i+1] = '\0';

	for(i = strlen(outname) - 1; i >= 0 && outname[i] == '/'; i--)
		;
	
	outname[i+1] = '\0';
	p = path;
	dir = root;
	if (*p == '/')
		p++;

	while ((s = strchr(p, '/')) != NULL) {
		*s = '\0';
		t = check_existance(dir, p);
		if (!t)
			dir = create_file(dir, p, Dmdir|0770, qpath++ * 16,
					  &dir_ops, NULL, NULL, NULL);
		else
			dir = t;

		if (!dir) {
			free(p);
			return -1;
		}

		*s = '/';
		p = s + 1;
	}

	if (p == path)
		nprefix = "";
	else {
		s = strrchr(path, '/');
		if (s) {
		        nprefix = sp_malloc(s - path + 1);
			snprintf(nprefix, s - path + 1, "%s", path);
		} else {
			nprefix = sp_malloc(strlen(path) + 1);
			strcpy(nprefix, path);
		}

		*(p-1) = '\0';
	}
	
	i = netread(dir, outname, nprefix, p);
	if (strlen(nprefix) > 0) 
		free(nprefix);

	return i;
}

static int
netmount(char *address, Spuser *user, int port)
{
	int n;
	char buf[32], *s;

	masterfs = spc_netmount(address, user, port, NULL, NULL);
	if (!masterfs)
		return -1;

	logfid = spc_open(masterfs, "log", Ordwr);
	if (!logfid) {
		spc_umount(masterfs);
		masterfs = NULL;
		return -1;
	}

	if (debuglevel == 0) {
		n = spc_read(logfid, (u8 *) buf, sizeof(buf), 0);
		if (n < 0)
			return -1;

		buf[n] = '\0';
		n = strtoul(buf, &s, 0);
		if (*s == '\0') {
			debuglevel = n;
			if (srv)
				srv->debuglevel = debuglevel & Dbgfs;

			spc_chatty = debuglevel & Dbgclnt;
		}
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int i, fileargs, len;
	pid_t pid;
	int c, ecode;
	char *s, *dlname, *ename;
	Spcfid *favail;
	File *f, *f1;
	struct stat st;
	Spuserpool *upool;

	files = NULL;
	clients = NULL;
	servers = NULL;
	starttime = time(NULL);
	masterfs = NULL;
	favail = NULL;
	servicetime = endtime = 0;
	xget_ename = NULL;
	xget_ecode = 0;
	
	while ((c = getopt(argc, argv, "D:p:n:w:r:m:sox")) != -1) {
		switch (c) {
		case 'p':
			port = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'D':
			debuglevel = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'n':
			netaddress = optarg;
			break;
		case 'r':
			maxretries = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv[0]);
			break;
		case 's':
			changeperms = 0;
			break;
		case 'o':
			rootonly = 1;
			changeperms = 0;
			break;
		case 'x':
			skipmnt = 1;
			break;
		case 'm':
			maxconnections = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv[0]);
			break;
		default:
			usage(argv[0]);
		}
	}

	spc_chatty = debuglevel & Dbgclnt;
	if (rootonly) {
		upool = sp_priv_userpool_create();
		if (!upool)
			goto error;

		user = sp_priv_user_add(upool, "root", 0, NULL);
		group = sp_priv_group_add(upool, "root", 0);
		sp_priv_user_setdfltgroup(user, group);
	} else {

		user = sp_unix_users->uid2user(sp_unix_users, geteuid());
		group = user->dfltgroup;
	}

	if (!user)
		goto error;
	if (!group)
		goto error;

	if (optind >= argc) {
		debug(Dbgfn, "Please specify at least one filename.\n");
		usage(argv[0]);
	}

	signal(SIGHUP, reload);
	
	if (netaddress) {
		if (netmount(netaddress, user, port) < 0) {
			fprintf(stderr, "Could not connect to server\n");
			goto error;
		}

		port = 0;	/* if slave, listen on any free port */
	}

	fsinit();
	fileargs = argc - optind;

        /* Parse file arguments for master*/
	if (!netaddress) {
		i = argc - 1;
		if(fileargs != 1) {
			debug(Dbgfn, "Please specify file or directory to serve.\n");
			usage(argv[0]);
		}
		
		if(stat(argv[i], &st) < 0) {
			sp_uerror(errno);
			goto error;
		}

		if (skipmnt)
			fs_dev = st.st_dev;
		
		len = strlen(argv[i]) + 1;
		path = (char *) malloc(len * sizeof(char));
		snprintf(path, len, "%s", argv[i]);
		if (path[len-2] == '/' && len > 2)
			path[len-2] = '\0';
		
		if (S_ISREG(st.st_mode))
			singlefile = 1;
		
		debug(Dbgfn, "server set path to %s\n", path);
	}

	/* Start server */
	srv = sp_socksrv_create_tcp(&port);
	if (!srv)
		goto error;

	spfile_init_srv(srv, root);
	srv->connopen = connopen;
	srv->connclose = connclose;
	srv->debuglevel = debuglevel & Dbgfs;
	srv->dotu = 0;
	if (rootonly)
		srv->upool = upool;
	else 
		srv->upool = sp_unix_users;

	srv->flush = xflush;
	sp_srv_start(srv);
	debug(Dbgsrvfn, "listen on %d\n", port);

	/* Parse file arguments for client*/
	if(netaddress) {
		if(fileargs < 2) {
			debug(Dbgfn, "Please specify at least a destination.\n");
			usage(argv[0]);
		}
		
		/* Last argument is destination */
		outname = argv[argc - 1];
		if(stat(outname, &st) != 0) {
			if (mkdir(outname, 0700) < 0) {
				sp_uerror(errno);
				debug(Dbgfn, "Destination does not exist and cannot be created.\n");
				goto error;
			}
		}

		if(!stat(outname, &st)) {
			if(!S_ISDIR(st.st_mode) && fileargs > 2) {
				debug(Dbgfn, "Plural sources and nondirectory destination.\n");
				usage(argv[0]);
			}
		} else {
			sp_uerror(errno);
			debug(Dbgfn, "Destination does not exist and cannot be created.\n");
			goto error;
		}


		for(i = optind; i < argc - 1; i++) {
			dlname = argv[i];
			if (!strcmp(".", dlname))
				dlname = "";
				
			if (netpathread(dlname) < 0)
				goto error;
			
			if (!strlen(dlname))
				break;
		}

		alarm(1);
		signal(SIGALRM, sigalrm);
	}

	/* don't go in the background if slave */
	if (!masterfs && !debuglevel && !spc_chatty) {
		close(0);
		close(1);
		close(2);
		fclose(stdin);
		fclose(stdout);
		fclose(stderr);

		pid = fork();
		if (pid < 0) {
			debug(Dbgfn, "cannot fork\n");
			return -1;
		}

		if (pid != 0) {
			/* parent */
			return 0;
		}

		/* child */
		setsid();
		chdir("/");
	}
		
	if (!masterfs) 
		localfileread(root, path);

	while (1) {
		if (xget_haserror())
			sp_werror(xget_ename, xget_ecode);

		if (sp_haserror() )
			goto error;

		if (ticksig) {
			if (tick() < 0)
				goto error;

			ticksig = 0;
		}

		sp_poll_once();
	}

	return 0;

error:
	sp_rerror(&ename, &ecode);
	if (ename)
		debug(Dbgfn, "Fatal error: %s\n", ename);
	
	if (path)
		free(path);

	f = files;
	while (f != NULL) {
		/*if (f->datafd)
		  spcfd_remove(f->datafd);
		  if (f->datafid)
		  spc_close(f->datafid);
		  if (f->availfid)
			spc_close(f->availfid);

		if (netaddress && f->fs && f->fs != masterfs)
			spc_umount(f->fs);
	  */
		f1 = f->next;
		free(f);
		f = f1;
	}
	
	if (masterfs)
		spc_umount(masterfs);
	
	return -1;
}

static void
connopen(Spconn *conn)
{
	Client *c;
	int n;

	numconnects++;
	debug(Dbgsrvfn, "Client %s connects %d\n", conn->address, numconnects);
	if (netaddress)
		return;

	n = strlen(conn->address) + 1;
	c = sp_malloc(sizeof(Client) + n + sizeof(int) * (maxlevels - 1));
	if (!c)
		return;


	c->caddress = (char *) c + sizeof(Client);
	if (!c->caddress) {
		free(c);
		return;
	}

	memcpy(c->caddress, conn->address, n);
	c->clevels = (int *) ((char *) c->caddress + n);
	if (!c->clevels) {
		free(c);
		return;
	}

	memset(c->clevels, 0, sizeof(int) * (maxlevels - 1));
	c->workersused = NULL;
	c->prev = NULL;
	c->next = clients;
	if (clients)
		clients->prev = c;

	clients = c;
}

static void
connclose(Spconn *conn)
{
	Client *c;
	Usedworker *uw, *uw2;
	
	numconnects--;
	debug(Dbgsrvfn, "Client %s disconnects %d\n", conn->address, numconnects);	
	if (netaddress)
		return;

	for (c = clients; c != NULL; c = c->next) {
		if (!strcmp(c->caddress, conn->address))
			break;
	}

	if (!c)
		return;

	for (uw = c->workersused; uw != NULL; ) {
		if (uw->worker && uw->worker->server &&
		    uw->worker->server->conns > 0)
			uw->worker->server->conns--;
		
		uw2 = uw->next;
		free(uw);
		uw = uw2;
	}

	if (c->next)
		c->next->prev = c->prev;
	if (c->prev)
		c->prev->next = c->next;
	if (c == clients)
		clients = c->next;

	debug(Dbgsrvfn, "Removing client with address: %s\n", c->caddress);
	free(c);
}

static int
redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n, dl, conns, level;
	char buf[128];
	File *f;
	Worker *w, *p, *t;
	Client *c;
	Usedworker *uw;

	f = fid->file->aux;

	if (offset || !f)
		return 0;

	memset(buf, 0, sizeof(buf));
	dl = 0; //The desired level in the tree

	//Find this client and determine the desired level
	for (c = clients; c != NULL; c = c->next) { 
		if (!strcmp(c->caddress, fid->fid->conn->address)) {
			for (n = 1; n < maxlevels - 1; n++) {
				if (c->clevels[dl] > c->clevels[n])
					dl = n;
			}
			
			break;
		}
	}

	dl++;
	if (!c) 
		return -1;

	level = 1;
	conns = 0;
	if (!f->nextworker)
		snprintf(buf, sizeof(buf), "me");
	else {
		p = f->nextworker;
		t = NULL;
		for (w = f->nextworker; w != NULL; ) {
			if (w->slevel >= maxlevels) {
				continue;
			}
			
			/*Try to find a worker with the desired level and the least number
			  of connections.  If the desired level cannot be found, just find
			  a worker with the least number of connections. */

			if (!t && w->server) {
				if (w->server->conns < maxconnections &&
				    dl == w->slevel)
					t = w;
				
				else if (w->server->conns < maxconnections) 
					t = w;
			}
			else if (w->server) {
				if (t->server->conns > w->server->conns && 
				    dl == w->slevel)
					t = w;
				
				else if (t->slevel != dl && t->server->conns >
					 w->server->conns)
					t = w;
			}

			if (!w->next && p == f->firstworker)
				break;
			else if (!w->next) 
				w = f->firstworker;
			else if (w->next == p)
				break;
			else
				w = w->next;
		}

		if (t) {
			snprintf(buf, sizeof(buf), "%s", t->server->saddress);
			if (t == f->lastworker)
				f->nextworker = f->firstworker;
			else 
				f->nextworker = t->next;
			
			c->clevels[t->slevel - 1]++;
			conns = t->server->conns + 1;
			level = t->slevel;
			uw = sp_malloc(sizeof(Usedworker));
			if (!uw) 
				return -1;

			uw->worker = t;
			uw->prev = NULL;
			uw->next = c->workersused;
			if (c->workersused)
				c->workersused->prev = uw;

			c->workersused = uw;
		}
		else
			snprintf(buf, sizeof(buf), "me");
	}
	
	n = strlen(buf);
	if (count > n)
		count = n;

	memcpy(data, buf, count);
	debug(Dbgsrvfn, "Client %s redirected to %s with conns: %d and level: %d" 
	      " for file: %s\n", fid->fid->conn->address, buf, conns, level, f->nname);

	return count;
}

static int
data_read(Spfilefid *fid, u64 offset, u32 count, u8 *buf, Spreq *r)
{
	Req *req;
	File *f = fid->file->aux;
	
	if (!f)
		return -1;

	req = sp_malloc(sizeof(*req));
	if (!req)
		return -1;

	req->offset = offset;
	req->count = count;
	req->req = r;
	req->prev = NULL;
	req->next = f->reqs;
	if (req->next)
		req->next->prev = req;
	f->reqs = req;

	respondreqs(f);
	return -1;
}

static int
checksum_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *r)
{
	File *f;
	char buf[11];

	f = fid->file->aux;
	if (!f)
		return 0;

	snprintf(buf, sizeof(buf), "%u", f->checksum);
	return cutstr(data, offset, count, buf, 0);
}

static int
avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n, port, slevel;
	char *s, *p, *address;
	Worker *worker, *wc;
	File *f;
	Server *server;

	f = fid->file->aux;
	if (offset || !f)
		return 0;

	server = NULL;
	address = NULL;
	s = strchr((const char *)data, ' ');
	if (!s) {
		sp_werror("invalid format for avail file", EINVAL);
		return -1;
	}
	
	*s++ = '\0';
	port = strtoul((const char *)data, &p, 0);
	if (*data == '\0' || *p != '\0') {
		sp_werror("invalid port number", EINVAL);
		return -1;
	}
	
	slevel = 0;
	if (!strcmp(s, "me")) {
		slevel = 1;
	}
	else {
		address = sp_malloc(strlen(s) + 1);
		strcpy(address, s);
		for (wc = f->firstworker; wc != NULL; wc = wc->next) {
			if (!strcmp(wc->server->saddress, address)) {
				slevel = wc->slevel + 1;
				wc->server->conns++;
				break;
			}
		}
		
		if (!slevel) {
			debug(Dbgsrvfn, "Could not find worker with address: %s\n",
			      address);
			free(address);
			return 0;
		}

		free(address);
		if (slevel >= maxlevels) {
			debug(Dbgsrvfn, "Tree level is too large, not adding client: %s"
			      " as a worker\n", req->conn->address);
			return count;
		}
	}
	
	p = strchr(req->conn->address, '!');
	if (!p) {
		sp_werror("Port number not given by client", EINVAL);
		return -1;
	}
	
	n = p - req->conn->address;
	address = sp_malloc(n + 7);
	if (!address)
		return -1;
       
	strncpy(address, req->conn->address, n);
	snprintf(address+n, 7, "!%d", port);
	for (wc = f->firstworker; wc != NULL; wc = wc->next) {
		if (!strcmp(wc->server->saddress, address)) {
			debug(Dbgsrvfn, "Found a duplicate worker, Address: %s." 
			      "  Not adding to worker list.\n", req->conn->address);
			goto error;
		}
	}

	worker = sp_malloc(sizeof(*worker));
	if (!worker) 
		goto error;
	
	for (server = servers; server != NULL; server = server->next) {
		if (!strcmp(server->saddress, address))
			break;
	}
	
	if (!server) {
		n = strlen(address) + 1;
		server = sp_malloc(sizeof(*server) + n);
		if (!server) 
			goto error;

		server->saddress = (char *) server + sizeof(*server);
		if (!server->saddress)
			goto error;

		memcpy(server->saddress, address, n);
		server->conns = 0;
		server->next = servers;
		server->prev = NULL;
		if (servers)
			servers->prev = server;

		servers = server;
	}
	
	free(address);
       	//Check to make sure this isn't a duplicate worker
	worker->slevel = slevel;
	worker->server = server;
	worker->next = NULL;
	worker->prev = f->lastworker;
	debug(Dbgsrvfn, "new worker: %p with address %s and with level: %d for file: %s\n",
	      worker, worker->server->saddress, worker->slevel, f->nname);

	if (f->lastworker)
		f->lastworker->next = worker;

	if (!f->firstworker)
		f->firstworker = worker;

	f->lastworker = worker;
	if (!f->nextworker)
		f->nextworker = worker;

	fid->aux = worker;
	f->numworkers++;
	debug(Dbgsrvfn, "Total number of workers: %d for file: %s\n", 
	      f->numworkers, f->nname);

	return count;

error:
	if (address)
		free(address);
	if (server)
		free(server);

	return -1;
}

static void
avail_closefid(Spfilefid *fid)
{
	Worker *worker;
	File *f;
		
	f = fid->file->aux;
	worker = fid->aux;
	if (!f || !worker)
		return;

	removeworker(f, worker);
}

static int
avail_wstat(Spfile* file, Spstat* stat)
{
	return 1;
}

static Spfcall *
xflush(Spreq *req)
{
	File *f;

	for (f = files; f != NULL; f = f->next) {
		Req *r;
		for(r = f->reqs; r != NULL; r = r->next) {
			if (r->req == req) {
				if (r->prev)
					r->prev->next = r->next;
				else
					f->reqs = r->next;
				
				if (r->next)
					r->next->prev = r->prev;
				
				free(r);
				break;
			}
		}
	
		sp_respond(req, NULL);
		break;
	}

	return sp_create_rflush();
}

static int
log_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", debuglevel);
	return cutstr(data, offset, count, buf, 0);
}

static int
log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	fprintf(stderr, "Client %s: %.*s", fid->fid->conn->address, count, data);
	return count;
}
