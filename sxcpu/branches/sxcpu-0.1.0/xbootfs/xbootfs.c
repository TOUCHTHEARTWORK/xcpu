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

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"

#include "xcpu.h"
#include "xbootfs.h"

typedef struct Worker Worker;
typedef struct Req Req;

struct Worker {
	char*	ip;
	int	port;
	/* the worker is alive until this time. Time is in seconds. */
	time_t until;
	Worker*	prev;
	Worker*	next;
};

struct Req {
	u64	offset;
	u32	count;
	Spreq*	req;
	Req*	next;
	Req*	prev;
};

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
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
	.write = log_write,
};

Spcfsys *masterfs, *redirfs;
Spcfid *datafid;
Spcfid *logfid;
Spsrv *srv;
Spfile *root;
Spuser *user;
Spuserpool *up;
int port = XBOOT_PORT;
unsigned int debuglevel = 0;
char *file = 0;
char *netaddress = 0;
int datasize = 0;	/* size of the data */
int datalen = 0;	/* how many bytes we received so far */
char *data = 0;		/* content of the data */
int numconnects = 0;
int numtransfers = 0;
int numworkers = 0;
Spfile *datafile;
int servicetime = 10; /* serve/dl file for default: 10 seconds */
int maxservicetime = 10; /* save initial service time */
int maxworkers = 10;
int servicetimedone = 0; /* kludge for now -- replace if you can */
int maxretries = 1; /* give at least 1 attempt by default */
int numretries = 0; /* how many attempts so far */
int retrytime = 10; /* wait default of 10 seconds before trying again */
int maxretrytime = 300; /* wait max of 5 minutes before trying again */
int servicetimeexts = 0; /* number of times to do logarithmic dl time extensions */   
char *outfilename;
Worker *firstworker, *lastworker, *nextworker;
Req *reqs;

static void 
usage(char *name) {
	fprintf(stderr, "usage as the master server: %s [-D debuglevel] [-p port] [-w maxworkers] <-f file>\n", name);
	fprintf(stderr, "usage as a client         : %s [-D debuglevel] [-p port] [-o filename] [-s servicetime] <-n netaddr>\n", name);
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

void removeworker(Worker *worker) {
	Worker *cur;

	if(!worker)
		return;

	/* In the case where a pointer was not set to null, this will ensure
	   that we do not try to free a dangling pointer
	*/
	for(cur = firstworker; cur; cur = cur->next) {
		if(cur == worker)
			break; 	/* Continue with delete */
	}
	if(!cur)
		return; /* Not found in queue, already removed */

	debug(Dbgfn, "worker %p is done\n", worker, worker->ip, worker->port);
	if (worker == firstworker)
		firstworker = worker->next;

	if (worker == lastworker)
		lastworker = worker->prev;

	if (worker == nextworker)
		nextworker = worker->next;

	if (worker->next)
		worker->next->prev = worker->prev;
	if (worker->prev)
		worker->prev->next = worker->next;

	numworkers--;
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

	if (file)
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
servicetimeout(int sig) {
	if(datalen < datasize && servicetimeexts > 0) {
		/* Ignore signal, reinstall timer at lower timeout */
		servicetime = (servicetime > 2) ? (servicetime/2) : 1;
		alarm(servicetime);
		servicetimeexts--;
	}
	else {
		if(servicetimeexts <= 0) {
	 		/* failed downloading within allowed time - use kludge to retry :( */
			servicetimedone = 1;
			return;
		}
		else {
			/* File written - Perhaps verify */
			exit(EXIT_SUCCESS);
		}
	}
}

static Spfile*
dir_first(Spfile *dir)
{
	spfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Spfile*
dir_next(Spfile *dir, Spfile *prevchild)
{
	spfile_incref(prevchild->next);
	return prevchild->next;
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

	ret->atime = ret->mtime = time(NULL);
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
	spfile_incref(ret);
	return ret;
}

static void
fsinit(void)    
{      
	Spfile *file;
	root = spfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	spfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;

	datafile = create_file(root, "data", 0444, Qdata, &data_ops, NULL, NULL);
	datafile->length = datasize;
	create_file(root, "avail", 0666, Qavail, &avail_ops, NULL, NULL);
	create_file(root, "log", 0222, Qlog, &log_ops, NULL, NULL);
	file = create_file(root, "redir", 0444, Qredir, &redir_ops, NULL, NULL);
	/* make it too long, it matters not ... */
	file->length = 32;
}

static int
localfileread(char *filename)
{
	int fd;
	struct stat st;

	if (stat(filename, &st) < 0) {
		sp_uerror(errno);
		goto error;
	}

	datasize = st.st_size;
	data = sp_malloc(datasize);
	if (!data)
		goto error;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		sp_uerror(errno);
		goto error;
	}

	if (read(fd, data, datasize) < datasize){
		sp_werror("couldn't read all the data from the file", EIO);
		goto error;
	}
	datalen = datasize;

	return 0;

error:
	if(data) 
	        free(data);
	datasize = 0;
	return -1;
}

static void
localfilewrite(void)
{
	int n, fd;

	/* save the file locally */
	if (outfilename) {
		fd = open(outfilename, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0) {
			sp_uerror(errno);
			return;
		}

		n = write(fd, data, datasize);
		if (n < 0) {
			sp_uerror(errno);
			return;
		}
		close(fd);
	}
}

static void
respondreqs(void)
{
	int count;
	Req *req, *nreq;
	Spfcall *rc;

	req = reqs;
	while (req != NULL) {
		count = datalen - req->offset;
		if (count < 0)
			count = 0;

		if (datalen==datasize || count>4096) {
			/* if we haven't got the whole file and can send back
			   only small chunk, don't respond, wait for more data */
			if (count > req->count)
				count = req->count;

			rc = sp_create_rread(count, (u8 *) data + req->offset);
			sp_respond(req->req, rc);
			nreq = req->next;
			if (req->prev)
				req->prev->next = req->next;
			else
				reqs = req->next;

			if (req->next)
				req->next->prev = req->prev;
			free(req);
			req = nreq;
		} else
			req = req->next;
	}
}

static void
netreadcb(Spcfd *fd, void *a)
{
	int n;

	n = spcfd_read(fd, data + datalen, datasize - datalen);
	if (n < 0) {
		if (redirfs != masterfs) {
			/* reading from a slave failed,
			   try reading from the master */
			spcfd_remove(fd);
			spc_close(datafid);
			sp_werror(NULL, 0);
			datafid = spc_open(masterfs, "data", Oread);
			spcfd_add(datafid, netreadcb, NULL);
			datalen = 0;
			return;
		}

		/* we were reading from the master, let the error be set,
		   the loop in main will check and exit */
	}

	datalen += n;
	if (datalen >= datasize) {
		spcfd_remove(fd);
		spc_close(datafid);
		datafid = NULL;
		localfilewrite();
	}

	respondreqs();
}

static int
netfileread(Spcfsys *masterfs)
{
	int n;
	Spwstat *st;
	Spcfid *f;
	char *s, *p, *dlevel, buf[128];

	st = NULL;
	f = NULL;
	redirfs = NULL;

	/* open the "log" file used to report errors,
	   don't return error if we get error */
	logfid = spc_open(masterfs, "log", Owrite);

	/* read the data size from the master */
	st = spc_stat(masterfs, "data");
	if (!st)
		goto error;
	datasize = st->length;
	datalen = 0;
	data = sp_malloc(datasize);
	if (!data)
		goto error;

	/* check if we should go somewhere else for the file */
	f = spc_open(masterfs, "redir", Oread);
	if (!f)
		goto error;

	n = spc_read(f, (u8 *) buf, sizeof(buf), 0);
	if (n < 0)
		goto error;

	buf[n] = '\0';
	spc_close(f);
	f = NULL;
	s = buf;
	if (strncmp(buf, "help ", 5)) {
		/* the server doesn't need help, set service time to zero */
		/* changing this behavior, but may put this back in --kevint */
		/* servicetime = 0; */
	} else {
		time_t request;
		s = buf + 5;
		request = strtoul(s, &s, 0);
		if (*s != ' ') {
			sp_werror("invalid format", EIO);
			goto error;
		}

		s++;
		servicetime = request < servicetime ? request : servicetime;
	}

	dlevel = strrchr(s, ' ');
	if (dlevel) {
		*dlevel = '\0';
		dlevel++;
		n = strtoul(dlevel, &p, 0);
		if (*p != '\0') {
			sp_werror("invalid format", EIO);
			goto error;
		}

		debuglevel = n;
	}

	if (!strcmp(s, "me"))
		redirfs = masterfs;
	else {
		debug(Dbgfn, "redirected to %s\n", buf);
		redirfs = spc_netmount(s, user, port, NULL, NULL);
		if (!redirfs)
			goto error;
	}

again:
	/* this can be more elegant, but retry log2(time) tries, this will 
	 * allow a download to take a maximum of 2 times the recommended or
	 * requested service time -- bad idea?
	 */
	servicetimeexts = (log(maxservicetime)/log(2));
	datafid = spc_open(redirfs, "data", Oread);
	if (!datafid)
		goto error;

	spcfd_add(datafid, netreadcb, NULL);
	return 0;

error:
	if (f) {
		f = NULL;
		spc_close(f);
	}

	if (redirfs && redirfs != masterfs)
		spc_umount(redirfs);

	/* if for any reason we got an error from a slave,
	   try again with the master */
	if (redirfs != masterfs) {
		redirfs = masterfs;
		goto again;
	}

	if(data)
	        free(data);
	data = NULL;
	datasize = 0;
	return -1;
}

static int
attemptfileread(char* address, Spuser *user, int port) {
 	/* Reset for reentry */
 	if(masterfs) {
 	   	servicetimedone = 0;
 		spc_umount(masterfs);
 		masterfs = NULL;
	}
 
 	while(numretries < maxretries) {
 	        numretries++;
 
		debug(Dbgclnt, "Attempting to contact master at %s\n", address);
 		
 		masterfs = spc_netmount(address, user, port, NULL, NULL);
 
 		servicetime = maxservicetime;
 		if(!masterfs || (netfileread(masterfs) < 0)) {
 			fprintf(stderr, "Unable to download, retrying in %d seconds\n", retrytime);
 			sleep(retrytime);
 			retrytime *= 1.5;
 			retrytime = (retrytime < maxretrytime) ? retrytime : maxretrytime;
 			if(masterfs)
 				spc_umount(masterfs);
 			continue;
 		}
 		else
 			break;
 	}
 	/* Number of retries exceeded */
 	if(numretries == maxretries) {
 	  fprintf(stderr, "Max attempts(%d) met, exiting!\n",  maxretries);
 	  return -1; 	  
 	}

 	/* Set alarm for download */
 	alarm(servicetime);
 	signal(SIGALRM, servicetimeout);
 	
 	/* File mounted */
 	return 0;
}

int
main(int argc, char **argv)
{
	pid_t pid;
	int c, ecode, n;
	char *s, *ename, buf[128];
	Spuserpool *upool;
	Spgroup *group;
	Spcfid *favail;

	masterfs = NULL;
	favail = NULL;
	outfilename = "/tmp/xboot";
	upool = sp_priv_userpool_create();
	if (!upool)
		goto error;

	while ((c = getopt(argc, argv, "D:p:f:n:s:o:w:r:")) != -1) {
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
		case 'f':
			file = optarg;
			break;
		case 'n':
			netaddress = optarg;
			break;

		case 's':
			maxservicetime = servicetime = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv[0]);
			break;
		case 'o':
			outfilename = optarg;
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

	if ((!file) && (!netaddress)) {
		fprintf(stderr, "please supply either -f or -n arguments\n");
		usage(argv[0]);
	}

	if (optind != argc)
		usage(argv[0]);

	signal(SIGHUP, reload);
	
	if (netaddress) {
		/* This section now calls this retryable function: */
		if(attemptfileread(netaddress, user, port) < 0)
			goto error;

		/* if slave, listen on any free port */
		port = 0;
	} else {
		if (localfileread(file) < 0)
			goto error;
	}

	if (servicetime <= 0)
		goto done;

	fsinit();

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

	if (masterfs) {
		favail = spc_open(masterfs, "avail", Owrite);
		if (!favail)
			goto error;

		/* why is it we don't print our IP here?
		 * because we should not trust a client. We will trust
		 * their port, but their IP is determined by the server
		*/
		snprintf(buf, sizeof(buf), "%d %d", port, servicetime);
		n = spc_write(favail, (u8 *) buf, strlen(buf), 0);
		if (n < 0) {
			goto error;
		}

		debug(Dbgfn, "listen on %d\n", port);
	}

	/* don't go in the background if slave */
	if (!masterfs && !debuglevel && !spc_chatty) {
		close(0);
		close(1);
		close(2);

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "cannot fork\n");
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

done:
	while (servicetime || datalen < datasize) {
		if(servicetimedone) {
			/* Max time extentions for one attempts met */
			fprintf(stderr, "Service Time done, retrying\n");
	    
			if(attemptfileread(netaddress, user, port) < 0) {
				fprintf(stderr, "Unable to download file, giving up\n");
				goto error;
			}
		}
		sp_rerror(&ename, &ecode);
		if (ecode != 0)
			break;
		
		sp_poll_once();
	}

	if (masterfs && favail)
		spc_close(favail);

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
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

	if (offset)
		return 0;

	memset(buf, 0, sizeof(buf));

	/* take this opportunity to clean up the workers */

	while (nextworker && (nextworker->until < now())) {
		removeworker(nextworker);
	}

	if (numworkers < maxworkers)
		snprintf(help, sizeof(help), "help %d ", servicetime);
	else
		help[0] = 0;
		
	if (!nextworker) {
		snprintf(buf, sizeof(buf), "%sme %d", help, debuglevel);
		/* nextworker = firstworker; */
	} else {
		snprintf(buf, sizeof(buf), "%s%s!%d %d", help, nextworker->ip,
			nextworker->port, debuglevel);
		if(nextworker == lastworker)
			nextworker = firstworker;
		else 
			nextworker = nextworker->next;
	}

	n = strlen(buf);
	if (count > n)
		count = n;

	memcpy(data, buf, count);
	debug(Dbgclnt, "Client %s redirected to %s\n", fid->fid->conn->address, buf);
	return count;
}

static int
data_read(Spfilefid *fid, u64 offset, u32 count, u8 *buf, Spreq *r)
{
	Req *req;

	req = sp_malloc(sizeof(*req));
	if (!req)
		return -1;

	req->offset = offset;
	req->count = count;
	req->req = r;
	req->prev = NULL;
	req->next = reqs;
	if (req->next)
		req->next->prev = req;
	reqs = req;

	respondreqs();
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
avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n, port;
	char *p;
	Worker *worker;
	int serviceoffer;

	if (offset)
		return 0;

	if ((port = strtoul((const char *)data, &p, 0)) == 0) {
		sp_werror("invalid port number", EINVAL);
		return -1;
	}

	if (numworkers >= maxworkers) {
		/* Not an error condition */
		debug(Dbgfn, "Not accepting any more workers\n", numworkers);
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
	worker->prev = lastworker;
	serviceoffer = serviceoffer < servicetime? serviceoffer : servicetime;
	worker->until = future(serviceoffer);

	debug(Dbgfn, "new worker %p: address %s port %d until %#x\n", worker, worker->ip, worker->port, worker->until);
	if (lastworker)
		lastworker->next = worker;

	if (!firstworker)
		firstworker = worker;

	lastworker = worker;
	if (!nextworker)
		nextworker = worker;

	fid->aux = worker;
	numworkers++;

	return count;
}

static void
avail_closefid(Spfilefid *fid)
{
	Worker *worker;

	worker = fid->aux;
	if (!worker)
		return;

	removeworker(worker);
}

static int
avail_wstat(Spfile* file, Spstat* stat)
{
	return 1;
}

static Spfcall *
xflush(Spreq *req)
{
	Req *r;

	for(r = reqs; r != NULL; r = r->next) {
		if (r->req == req) {
			if (r->prev)
				r->prev->next = r->next;
			else
				reqs = r->next;

			if (r->next)
				r->next->prev = r->prev;

			free(r);
			break;
		}
	}

	sp_respond(req, NULL);
	return sp_create_rflush();
}

static int
log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	fprintf(stderr, "Client %s: %.*s", fid->fid->conn->address, count, data);
	return count;
}
