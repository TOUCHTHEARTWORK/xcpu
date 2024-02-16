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

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"

#include "xcpu.h"
#include "xbootfs.h"

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

Spfileops crc_ops = {
	.read = crc_read,
};

Spcfsys *masterfs;
Spcfid *logfid;
Spsrv *srv;
Spfile *root;
Spuser *user;
Spuserpool *up;
int port = XBOOT_PORT;
unsigned int debuglevel = 0;
char *netaddress = 0;
int numconnects = 0;
int numtransfers = 0;
int maxworkers = 10;
int maxretries = 1; /* give at least 1 attempt by default */
int numretries = 0; /* how many attempts so far */
int retrytime = 10; /* wait default of 10 seconds before trying again */
int maxretrytime = 300; /* wait max of 5 minutes before trying again */
char *path, *outname;
int recursive = 0; /* Do not explore directories */
int ticksig;
u64 qpath = 1;
File *files;

time_t starttime;
time_t endtime;
time_t servicetime;

static int netpathread(Spfile *parent, char *prefix, char *path);
static int fullfilename(Spfile *file, char* fullname, int buflen);
static void netreadcb(Spcfd *fd, void *a);

static void 
usage(char *name) {
	fprintf(stderr, "usage as the master server: %s [-D debuglevel] [-p port] %s", 
		name, "[-w maxworkers] file|directory\n");
	fprintf(stderr, "usage as a client         : %s [-D debuglevel] [-p port] %s %s", 
		name, "[-s servicetime] <-n netaddr> <src file | src dir | .> ", 
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

	debug(Dbgfn, "worker %p is done\n", worker, worker->ip, worker->port);
	if (worker == f->firstworker)
		f->firstworker = worker->next;

	if (worker == f->lastworker)
		f->lastworker = worker->prev;

	if (worker == f->nextworker)
		f->nextworker = worker->next;

	if (worker->next)
		worker->next->prev = worker->prev;
	if (worker->prev)
		worker->prev->next = worker->next;

	f->numworkers--;
	free(worker->ip);
	free(worker);
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
	alarm(1);
	ticksig = 1;
	if (servicetime && time(NULL) > servicetime)
		exit(0);
}

static int
fileretry(File *f)
{
	char name[256];

	fullfilename(f->dir, name, sizeof(name));
	if (f->fs == masterfs) {
		sp_werror("can't retry", EIO);
		return -1;
	}

	f->datalen = 0;
	spcfd_remove(f->datafd);
	f->datafd = NULL;

	f->datafid = spc_open(masterfs, name, Oread);
	if (!f->datafid)
		return -1;

	f->datafd = spcfd_add(f->datafid, netreadcb, f);
	return 0;
}

static void
tick(void)
{
	int fdone, n;
	File *f;

	debug(Dbgfn, "tick\n");
	if (!endtime) {
		fdone = 1;
		for(f = files; f != NULL; f = f->next) {
			if (f->finished<0 && fileretry(f)<0)
				return;

			if (f->finished)
				continue;

			fdone = 0;
			if (f->progress>0 && fileretry(f) < 0)
				return;

			f->progress = 1;
		}

		if (fdone) {
			endtime = time(NULL);
			debug(Dbgfn, "finished download for %d seconds\n",
				(endtime - starttime));

			n = endtime - starttime;
			if (n < 5)
				n = 5;
			servicetime = endtime + n;
		}
	}
}

static void
dir_remove(Spfile *dir)
{
	Spfile *f, *parent;
	
	for(f = dir->dirfirst; f != NULL; f = f->next) {
		parent = f->parent;
		if (f->mode & Dmdir)
			dir_remove(f);
		else
			spfile_decref(f);

		if (parent)
			spfile_decref(parent);

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

		snprintf(fullname, bufleft, "%s", tbuf);
		bufleft -= len;
	}

	return(buflen - bufleft);
}

static Spfile*
updatedir(Spfile *dir, char *name)
{
	int blen, bsize, len;
	char *buf;
	struct stat st;
	Spfile *flist, *f, *f1;
	DIR *d;
	struct dirent *de;

	bsize = NAME_MAX + 1;
	blen = strlen(name);
	buf = sp_malloc(bsize);
	if (!buf)
		return NULL;
	strcpy(buf, name);

	/* Capture 9P directory entries.  Anything not removed from
	 * local directory entry matches will need to be pruned from
	 * the 9P directory.
	 */
	flist = dir->dirfirst;
	dir->dirfirst = NULL;
	dir->dirlast = NULL;
	d = opendir(name);
	if (!d) {
		sp_uerror(errno);
		return NULL;
	}

	while ((de = readdir(d))) {
		/* ignore "hidden" files */
		if (*de->d_name == '.')
			continue;

		len = strlen(de->d_name);
		if (blen+len+1 > bsize) 
			continue;
				
		snprintf(buf+blen, bsize-blen, "/%s", de->d_name);
		if (stat(buf, &st) < 0)
			goto error;

		/* skip files other than directories and regular files */
		if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
			continue;

		for(f = flist; f != NULL; f = f->next) {
			if (!strcmp(de->d_name, f->name)) {
				debug(Dbgfn, "9P file match %s\n", f->name);
				if (f == flist)
					flist = f->next;
				else
					f->prev->next = f->next;

				if (f->next)
					f->next->prev = f->prev;

				if (!dir->dirfirst)
					dir->dirfirst = f;
				else
					dir->dirlast->next = f;

				f->prev = dir->dirlast;
				f->next = NULL;
				dir->dirlast = f;

				debug(Dbgfn, "New file found, doing localfileread\n");
				if (localfileread(dir, buf, 0) < 0) {
					debug(Dbgfn, "Error in localfileread\n");
					sp_werror(NULL, 0);
				}

				break;
			}
		}
	}
	closedir(d);
	free(buf);

	/* now get rid of the files that no longer exist */
	f = flist;
	while (f != NULL) {
		f1 = f->next;
		debug(Dbgfn, "Stale file found, removing %s\n", f->name);
		parent_remove(f);
		dir_remove(f);
		f = f1;
	}

	return dir;

error:
	free(buf);
	if (d)
		closedir(d);

	return NULL;
}

static Spfile*
dir_update(Spfile *dir)
{
	char* name;
	Spfile *ret;
	struct stat st;
	int namelen, namesize;
	
	namelen = 0;
	namesize = NAME_MAX + 1;
	name = (char*) calloc(namesize, 1);
	strncpy(name, path, namesize);
	namelen = strlen(name);
	if(strlen(dir->name)) {
		debug(Dbgfn, "Calling fullfilename on %s\n", dir->name);
		if( (namelen += fullfilename(dir, name + namelen, namesize - namelen)) <= 0) {
			debug(Dbgfn, "Failed to find full 9p file name for %s\n", dir->name);
			goto error;
		}
	}

	if (stat(name, &st) < 0) {
		sp_werror(Enotfound, ENOENT);
		goto error;
	}

	if (st.st_mtime == dir->mtime) {
		debug(Dbgfn,"Directory or file not changed\n");
		free(name);
		return dir;
	}

	debug(Dbgfn, "Directory or file changed\n");
	if (S_ISREG(st.st_mode)) {
		if (localfileread(dir, name, 0) < 0) {
			sp_uerror(errno);
			goto error;
		}

		ret = dir;
	} else if (S_ISDIR(st.st_mode))
		ret = updatedir(dir, name);
	else {
		sp_werror(Eperm, EPERM);
		ret = NULL;
	}

	free(name);
	if (ret)
		dir->mtime = st.st_mtime;

	return ret;

error:
	free(name);
	return NULL;
}
		
static Spfile*
dir_first(Spfile *dir)
{
	if (recursive) {
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
	File *f;

	debug(Dbgfn, "Destroying file: %s\n", dir->name);
	f = (File *) dir->aux;
	if (f) {
		if (f == files) {
			files = f->next;
			files->prev = NULL;
		}
		else if (f->next && f->prev) {
			f->prev->next = f->next;
			f->next->prev = f->prev;
		}
		else if (f->prev && !f->next)
			f->prev->next = NULL;
		
		munmap(f->data, f->datasize);
		free(f);
	}	
}

static Spfile *
create_file(Spfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Spuser *usr, void *aux)
{
	Spfile *ret;

	ret = spfile_alloc(parent, name, mode, qpath, ops, aux);
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

	//	ret->atime = ret->mtime = time(NULL);
	ret->atime = ret->mtime = 0;
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
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
	create_file(root, "log", 0222, Qlog, &log_ops, NULL, NULL);
}

static File *
filealloc(Spfile *parent, char *name, int datasize, int datalen, char *data, 
	  u32 mtime, u32 crc)
{
	int qp;
	File *f;
	Spfile *datafile, *file;

	f = sp_malloc(sizeof(*f) + strlen(name) + 1);
	qp = qpath++ * 16;
	f->name = ((char *) f) + sizeof(*f);
	strcpy(f->name, name);
	f->datasize = datasize;
	f->datalen = datalen;
	f->data = data;
	f->numworkers = 0;
	f->firstworker = NULL;
	f->lastworker = NULL;
	f->nextworker = NULL;
	f->reqs = NULL;
	f->datafid = NULL;
	f->availfid = NULL;
	f->crc = crc;
	f->finished = 0;
	f->progress = 0;

	f->dir = create_file(parent, name, 0555 | Dmdir, (qp << 8), &dir_ops, NULL, f);
	f->dir->mtime = f->dir->atime = mtime;
	datafile = create_file(f->dir, "data", 0444, (qp << 8) | Qdata, &data_ops, NULL, f);
	datafile->length = datasize;
	create_file(f->dir, "avail", 0666, (qp<<8) | Qavail, &avail_ops, NULL, f);
	file = create_file(f->dir, "redir", 0444, (qp << 8) | Qredir, &redir_ops, NULL, f);
	file->length = 32;
	
	create_file(f->dir, "crc", 0444, (qp << 8) | Qcrc, &crc_ops, NULL, f);
	f->next = files;
	f->prev = NULL;
	if (files)
		files->prev = f;
	files = f;

	return f;
}

static int
localfileread(Spfile *parent, char *filename, int r)
{
	char *name, *data;
	struct stat st;
	File *f;
	Spfile *dir;
	char *nextf;
	DIR *dirstr;
	struct dirent *de;
	int fd;
	u32 crc;

	data = NULL;
	f = NULL;
	crc = 0;

	if ((name = strrchr(filename, '/'))) 
		name++;
	else 
		name = filename;

	if (stat(filename, &st) < 0) {
		sp_uerror(errno);
		return -1;
	}

	if (S_ISREG(st.st_mode)) {
		if (st.st_size != 0) {
			fd = open(filename, O_RDONLY);
			if (fd < 0) {
				sp_uerror(errno);
				return -1;
			}

			data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | 
				    MAP_FILE, fd, 0);
			if (data == (void *) -1) {
				sp_uerror(errno);
				return -1;
			}
			
			close(fd);
			crc = crc32(data, st.st_size);
		}
			
		f = filealloc(parent, name, st.st_size, st.st_size, data, 
			      st.st_mtime, crc);
		if (!f)
			return -1;
	} else if (S_ISDIR(st.st_mode)) {
		if (strcmp(path, filename)) {
			dir = create_file(parent, name, 0770 | Dmdir, qpath++ * 16,
				&dir_ops, NULL, NULL);
		} else {
			dir = root;
			dir->mtime = dir->atime = st.st_mtime;
		}
	
		if (r) {
			dirstr = opendir(filename);
			while ((de = readdir(dirstr))) {
				if(strncmp(de->d_name, ".", 1) == 0)
					continue;

				nextf = (char *) malloc(strlen(filename) + 
							strlen(de->d_name) + 2);
				sprintf(nextf, "%s/%s", filename, de->d_name);
				localfileread(dir, nextf, r);
				free(nextf);
			}
			closedir(dirstr);
		}		
	}	
		
	return 0;
}

static void
respondreqs(File *f)
{
	int count;
	Req *req, *nreq;
	Spfcall *rc;

	req = f->reqs;
	while (req != NULL) {
		count = f->datalen - req->offset;
		if (count < 0)
			count = 0;

		if (f->datalen==f->datasize || count>4096) {
			/* if we haven't got the whole file and can send back
			   only small chunk, don't respond, wait for more */
			if (count > req->count)
				count = req->count;

			rc = sp_create_rread(count, (u8 *) f->data + req->offset);
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
}

/* Calculates CRC-32 based on table in xbootfs.h.  Table and the crc32 function
   below were taken from: http://fxr.watson.org/fxr/source/libkern/crc32.c  */
static u32
crc32(const void *buf, size_t size)
{
	const u8 *p = buf;
	u32 crc;
	
	crc = ~0U;
	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ ~0U;
}

 
static void
netreadcb(Spcfd *fd, void *a)
{
	int n;
	File *f;
	int crc;

	f = a;
	n = spcfd_read(fd, f->data + f->datalen, f->datasize - f->datalen);
//	if (n < 0) {
//		if (redirfs != masterfs) {
			/* reading from a slave failed,
			   try reading from the master */
/*			spcfd_remove(fd);
			spc_close(f->datafid);
			sp_werror(NULL, 0);
			snprintf(buf, sizeof(buf), "%s/data", f->name);
			f->datafid = spc_open(masterfs, buf, Oread);
			spcfd_add(f->datafid, netreadcb, f);
			f->datalen = 0;
			return;
			} */

		/* we were reading from the master, let the error be set,
		   the loop in main will check and exit */
		
//	}

	f->datalen += n;
	f->progress = 0;
	if (f->datalen >= f->datasize) {
		spcfd_remove(fd);
//		spc_close(f->datafid);
//		f->datafid = NULL;
		msync(f->data, f->datasize, MS_SYNC | MS_INVALIDATE);
		crc = crc32(f->data, f->datasize);
		if (crc == f->crc)
			f->finished = 1;
		else
			f->finished = -1;
	}
	
	respondreqs(f);
}

static int
netfileread(Spfile *dir, char *path, int len, int mtime)
{
	int n, fd, blen, crc;
	char *buf, *lname, *fname, *s;
	void *data;
	Spcfid *fid, *datafid, *crcfid, *availfid;
	Spcfsys *redirfs;
	File *file;

	datafid = NULL;
	crcfid = NULL;
	availfid = NULL;
	redirfs = NULL;
	data = NULL;

	blen = strlen(path) + 10;
	if (blen < 128)
		blen = 128;
	buf = sp_malloc(blen);
	if (!buf)
		return -1;

	lname = sp_malloc(strlen(outname) + strlen(path) + 2);
	if (!lname)
		return -1;

	sprintf(lname, "%s/%s", outname, path);
	fd = open(lname, O_CREAT | O_TRUNC | O_RDWR, 0600);
	free(lname);
	if (fd < 0) {
		sp_uerror(errno);
		goto error;
	}

	if (ftruncate(fd, len) < 0) {
		sp_uerror(errno);
		close(fd);
		goto error;
	}

	data = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE, fd, 0);
	close(fd);
		
	if (data == (void *) -1) {
		sp_uerror(errno);
		data = NULL;
		goto error;
	}

	/* check if we should go somewhere else for the file */
	sprintf(buf, "%s/redir", path);
	fid = spc_open(masterfs, buf, Oread);
	if (!fid)
		goto error;

	n = spc_read(fid, (u8 *) buf, blen, 0);
	if (n < 0)
		goto error;

	buf[n] = '\0';
	spc_close(fid);
	fid = NULL;

	s = buf;
	if (strncmp(buf, "help ", 5) == 0)
		s = buf + 5;

	if (!strcmp(s, "me"))
		redirfs = masterfs;
	else {
		debug(Dbgfn, "redirected to %s\n", buf);
		redirfs = spc_netmount(s, user, port, NULL, NULL);
		if (!redirfs)
			goto error;
	}

	sprintf(buf, "%s/data", path);
	datafid = spc_open(redirfs, buf, Oread);
	if (!datafid)
		goto error;

	sprintf(buf, "%s/crc", path);
	crcfid = spc_open(masterfs, buf, Oread);
	if (!crcfid)
		goto error;

	n = spc_read(crcfid, (u8 *) buf, blen, 0);
	if (n < 0)
		goto error;

	buf[n] = 0;
	crc = strtoul(buf, NULL, 0);
	spc_close(crcfid);

	sprintf(buf, "%s/avail", path);
	availfid = spc_open(masterfs, buf, Owrite);
	if (!availfid)
		goto error;

	fname = strrchr(path, '/');
	if (!fname)
		fname = path;
	else
		fname++;

	file = filealloc(dir, fname, len, 0, data, mtime, 0);
	if (!file)
		goto error;

	file->fs = redirfs;
	file->data = data;
	file->datafid = datafid;
	file->availfid = availfid;
	file->crc = crc;
	file->datafd = spcfd_add(file->datafid, netreadcb, file);

	free(buf);
	return 0;

error:
	if (datafid)
		spc_close(datafid);

	if (crcfid)
		spc_close(crcfid);

	if (availfid)
		spc_close(availfid);

	if (redirfs && redirfs != masterfs)
		spc_umount(redirfs);

	if (data)
		munmap(data, len);

	free(buf);
	return -1;
}

static int
netdirread(Spfile *parent, char *path)
{
	int ret, i, n, plen;
	Spcfid *fid;
	Spwstat *st;

	plen = strlen(path);
	fid = spc_open(masterfs, path, Oread);
	if (!fid)
		return -1;

	while ((n = spc_dirread(fid, &st)) > 0) {
		for(i = 0; i < n; i++) {
			if (netpathread(parent, path, st[i].name) < 0) {
				ret = -1;
				free(st);
				goto done;
			}
		}

		free(st);
	}

	if (n < 0)
		ret = -1;
	else
		ret = 0;

done:
	spc_close(fid);
	return ret;	
}

static int
netpathread(Spfile *parent, char *prefix, char *path)
{
	int i, len, mtime, olen, ret;
	char *fname, *lname, *p, *s;
	struct stat ustat;
	Spwstat *st;
	Spfile *dir;

	fname = sp_malloc(strlen(prefix) + strlen(path) + 8);
	sprintf(fname, "%s/%s", prefix, path);

	olen = strlen(outname) + 1;
	lname = sp_malloc(olen + strlen(prefix) + strlen(path) + 2);
	sprintf(lname, "%s/%s/%s", outname, prefix, path);

	/* get rid of trailing '/' */
	for(i = strlen(fname) - 1; fname[i] == '/'; i--)
		;
	fname[i+1] = '\0';
	lname[olen+i+1] = '\0';

	/* first walk and create all directories in the path */
	p = fname + strlen(prefix) + 1;
	dir = parent;
	while ((s = strchr(p, '/')) != NULL) {
		*s = '\0';
		lname[olen + (s - fname)] = '\0';
		st = spc_stat(masterfs, fname);
		if (!st)
			goto error;

		if (!(st->mode & Dmdir)) {
			if (s) {
				*s = '/';
				lname[olen + (s - fname)] = '/';
			}

			break;
		}

		if (stat(lname, &ustat) < 0) {
			if (mkdir(lname, 0770) < 0) {
				sp_uerror(errno);
				goto error;
			}

			stat(lname, &ustat);
		}

		if (!S_ISDIR(ustat.st_mode)) {
			sp_werror(Enotdir, ENOTDIR);
			goto error;
		}

		dir = create_file(dir, p, Dmdir|0770, qpath++ * 16,
			&dir_ops, NULL, NULL);

		free(st);
		*s = '/';
		lname[olen + (s - fname)] = '/';
		p = s + 1;
	}

	s = fname + strlen(fname);
	strcat(fname, "/data");
	st = spc_stat(masterfs, fname);
	*s = '\0';
	if (!st) {
		/* if there is no "data" file, it could be a directory */
		sp_werror(NULL, 0);
		ret = netdirread(dir, fname);
	} else {
		len = st->length;
		mtime = st->mtime;
		free(st);
		st = NULL;

		/* regular file, set it up */
		ret = netfileread(dir, fname, len, mtime);
	}

	free(fname);
	free(lname);
	return ret;

error:
	free(fname);
	free(lname);
	free(st);
	return -1;
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
		
	return 0;
}

int
main(int argc, char **argv)
{
	int i, fileargs, len;
	pid_t pid;
	int c, ecode, n, fcount;
	char *s, *ename, buf[128], *dlname;
	Spuserpool *upool;
	Spgroup *group;
	Spcfid *favail;
	File *f, *f1;
	struct stat st;

	starttime = time(NULL);
	masterfs = NULL;
	favail = NULL;
	fcount = 0;

	upool = sp_priv_userpool_create();
	if (!upool)
		goto error;

	while ((c = getopt(argc, argv, "D:p:n:w:r:")) != -1) {
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
		case 'w':
			maxworkers = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv[0]);
			break;
		case 'r':
			maxretries = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv[0]);
			break;
		default:
			usage(argv[0]);
		}
	}

	spc_chatty = debuglevel & Dbgclnt;
	user = sp_priv_user_add(upool, "root", 0, NULL);
	if (!user)
		goto error;

	group = sp_priv_group_add(upool, "root", 0);
	if (!group)
		goto error;

	sp_priv_user_setdfltgroup(user, group);

	if (optind >= argc) {
		debug(Dbgfn, "Please specify at least one filename.\n");
		usage(argv[0]);
	}

	signal(SIGHUP, reload);

	if (netaddress) {
		if (netmount(netaddress, user, port) < 0)
			goto error;

		port = 0;	/* if slave, listen on any free port */
	}

	fsinit();

        /* Parse file arguments */
	fileargs = argc - optind;

	if(netaddress) {
		if(fileargs < 2) {
			debug(Dbgfn, "Please specify at least a destination.\n");
			usage(argv[0]);
		}
		
		/* Last argument is destination */
		outname = argv[argc - 1];
		if(stat(outname, &st) != 0) {
			sp_uerror(errno);
			debug(Dbgfn, "Destination does not exist.\n");
			goto error;
		}
		
		if(!S_ISDIR(st.st_mode) && fileargs > 2) {
			debug(Dbgfn, "Plural sources and nondirectory destination.\n");
			usage(argv[0]);
		}
		
		for(i = optind; i < argc - 1; i++) {
			dlname = argv[i];
			if (!strcmp(".", dlname)) {
				if (netpathread(root, "", NULL) < 0)
					goto error;	
				break;
			}
			if (netpathread(root, "", dlname) < 0)
				goto error;
		}

		alarm(1);
		signal(SIGALRM, sigalrm);
	}
	else {
		i = argc - 1;
		if(fileargs != 1) {
			debug(Dbgfn, "Please specify file or directory to serve.\n");
			usage(argv[0]);
		}
		
		if(stat(argv[i], &st) < 0)
			sp_uerror(errno);

		len = strlen(argv[i]) + 1;
		path = (char *) malloc(len * sizeof(char));
		snprintf(path, len, "%s", argv[i]);
		if (path[len-2] == '/')
			path[len-2] = '\0';

		debug(Dbgfn, "server set path to %s\n", path);
		if (S_ISDIR(st.st_mode))
			recursive = 1;
		
		if (localfileread(root, path, 1) < 0)
			goto error;
	}

	srv = sp_socksrv_create_tcp(&port);
	if (!srv)
		goto error;

	spfile_init_srv(srv, root);
	srv->connopen = connopen;
	srv->connclose = connclose;
	srv->debuglevel = debuglevel & Dbgfs;
	srv->dotu = 0;
	srv->upool = upool;
	srv->flush = xflush;
	sp_srv_start(srv);
	debug(Dbgfn, "listen on %d\n", port);

	if (masterfs) {
		for (f = files; f != NULL; f = f->next) {
			/* why is it we don't print our IP here?
			 * because we should not trust a client. We will trust
			 * their port, but their IP is determined by the server
			 */
			snprintf(buf, sizeof(buf), "%d %d", port, (int) servicetime);
			n = spc_write(f->availfid, (u8 *) buf, strlen(buf), 0);
			fcount++;
			if (n < 0) {
				goto error;
			}
		}
	}	
	
	/* don't go in the background if slave */
	if (!masterfs && !debuglevel && !spc_chatty) {
		close(0);
		close(1);
		close(2);

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

	while (1) {
		if (ticksig) {
			tick();
			ticksig = 0;
		}

		if (sp_haserror())
			goto error;

		sp_poll_once();
	}

	for(f = files; f != NULL; f = f->next) {
		if (f->datafid)
			spc_close(f->datafid);
		if (f->availfid)
			spc_close(f->availfid);
	}

	if (masterfs)
		spc_umount(masterfs);

	masterfs = NULL;
	free(path);
	return 0;

error:
	sp_rerror(&ename, &ecode);
	debug(Dbgfn, "%s\n", ename);
	if (path)
		free(path);

	f = files;
	while (f != NULL) {
		if (f->datafid)
			spc_close(f->datafid);
		if (f->availfid)
			spc_close(f->availfid);
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
	numconnects++;
	debug(Dbgclnt, "Client %s connects %d\n", conn->address, numconnects);
}

static void
connclose(Spconn *conn)
{
	numconnects--;
	debug(Dbgclnt, "Client %s disconnects %d\n", conn->address, numconnects);
}

static int
redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n;
	char buf[128], help[32];
	File *f;
	Worker *w;

	f = fid->file->aux;

	if (offset)
		return 0;

	memset(buf, 0, sizeof(buf));

	/* take this opportunity to clean up the workers */
	w = f->firstworker;
	while (w && (w->until < now())) {
		debug(Dbgfn, "Removing worker in redir_read for File: " 
		      "%s - ip: %s, port: %d, until: %ld\n", f->name, w->ip, 
		      w->port, w->until);
		removeworker(f, w);
		w = w->next;
	}

	if (f->numworkers < maxworkers) {
		snprintf(help, sizeof(help), "help ");
		debug(Dbgfn, "Asking for help in redir_read for File: %s\n", f->name);
	}
	else {
		help[0] = 0;
		debug(Dbgfn, "Removing worker in redir_read for File: " 
		      "%s - ip: %s, port: %d, until: %ld\n", f->name, w->ip, 
		      w->port, w->until);
	}		
		
	if (!f->nextworker) {
		snprintf(buf, sizeof(buf), "%sme", help);
		debug(Dbgfn, "Asking for help and download from master for file: %s\n", 
		      f->name);
		/* nextworker = firstworker; */
	} else {
		snprintf(buf, sizeof(buf), "%s%s!%d", help, f->nextworker->ip,
			 f->nextworker->port);
		debug(Dbgfn, "Asking for help and download from a different server for "
		      "file: %s, from: %s %d\n", f->name, f->nextworker->ip, f->nextworker->port);
		if (f->nextworker == f->lastworker)
			f->nextworker = f->firstworker;
		else 
			f->nextworker = f->nextworker->next;
	}

	n = strlen(buf);
	if (count > n)
		count = n;

	memcpy(data, buf, count);
	debug(Dbgclnt, "Client %s redirected to %s\n", fid->fid->conn->address, buf);
	debug(Dbgfn, "Client %s redirected to %s\n", fid->fid->conn->address, buf);
	return count;
}

static int
data_read(Spfilefid *fid, u64 offset, u32 count, u8 *buf, Spreq *r)
{
	Req *req;
	File *f = fid->file->aux;

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
/*
	if (offset == 0){
		numtransfers++;
		debug(Dbgclnt, "Client transfers %d\n", numtransfers);
	}
	if (offset > datasize) {
		return 0;
	}
	if ((offset + count) > datasize)
		count = datasize - offset;
	memcpy(buf, &data[offset], count);
	return count;
*/
}

static int
crc_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *r)
{
	File *f;
	int n;
	char buf[11];

	f = fid->file->aux;
	snprintf(buf, sizeof(buf), "%u", f->crc);
       	n = strlen(buf) + 1;
	if (count > n)
		count = n;
	
	memcpy(data, buf, count);
	return count;
}

static int
avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n, port;
	char *p;
	Worker *worker, *dwc;
	int serviceoffer;
	File *f;

	f = fid->file->aux;
	if (offset)
		return 0;

	if ((port = strtoul((const char *)data, &p, 0)) == 0) {
		sp_werror("invalid port number", EINVAL);
		return -1;
	}

	if (f->numworkers >= maxworkers) {
		/* Not an error condition */
		debug(Dbgfn, "Not accepting any more workers\n", f->numworkers);
		return count;
	}
	
	serviceoffer = strtoul(p, 0, 0);
	worker = sp_malloc(sizeof(*worker));
	if (!worker) 
		return -1;

	/* get the ip address of the client from the connection */
	p = strchr(req->conn->address, '!');
	if (!p)
		p = req->conn->address + strlen(req->conn->address);

	n = p - req->conn->address;
	worker->ip = sp_malloc(n + 1);
	memmove(worker->ip, req->conn->address, n);
	worker->ip[n] = '\0';
	worker->port = port;
	worker->next = NULL;
	worker->prev = f->lastworker;
	serviceoffer = serviceoffer < servicetime? serviceoffer : servicetime;
	worker->until = future(serviceoffer);
	debug(Dbgfn, "new worker %p: address %s port %d until %#x\n", worker, worker->ip, 
	      worker->port, worker->until);
	
	//Check to make sure this isn't a duplicate worker
	for (dwc = f->firstworker; dwc != NULL; dwc = dwc->next) {
		if (!strcmp(dwc->ip, worker->ip) && 
		    dwc->port == worker->port) {
			debug(Dbgfn, "Found a duplicate worker, IP: %s, Port: %d." 
			      "  Not adding to worker list.\n", worker->ip, worker->port);
			    free(worker);
			    return(count);
		    }
	}
			    
	if (f->lastworker)
		f->lastworker->next = worker;

	if (!f->firstworker)
		f->firstworker = worker;

	f->lastworker = worker;
	if (!f->nextworker)
		f->nextworker = worker;

	fid->aux = worker;
	f->numworkers++;
	return count;
}

static void
avail_closefid(Spfilefid *fid)
{
	Worker *worker;
	File *f;
	
	f = fid->file->aux;
	worker = fid->aux;
	if (!worker)
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
