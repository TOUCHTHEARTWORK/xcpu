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
#include <signal.h>
#include <regex.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"

#include "xcpu.h"
#include "queue.h"
#include "xbootfs.h"

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Spfileops ctl_ops = {
	.read = ctl_read,
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
       	.wstat = avail_wstat
};

Spuser *user;
Spuserpool *up;

struct Worker {
	char *ip;
	int  port;
};

int IAmMaster = 0;
Spsrv 	*srv;
Spfile 	*root;
char 	*defip = "127.0.0.1";
char 	*defproto = "tcp";
char	*service = "tcp!*!20003";
char 	*cfg = "xbootfs.conf";
int		defport = 20002;
unsigned int	debuglevel = 0;
Queue *avail;
char *file = 0, *netaddress = 0;
int filelen = 0;
char *data = 0;
Spfile *datafile;
int servicetime = 30; /* total of 30 seconds */

static void 
usage(char *name) {
	fprintf(stderr, "usage: %s [-d] [-D debuglevel] [-p port] <-f file> | <-n netaddr>\n", name);
	exit(1);
}

static void
debug(int level, char *fmt, ...)
{
	va_list arg;

	if (!(debuglevel & level))
		return;
	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);
}

void
timeout(int whatever)
{
	fprintf(stderr, "All done (%d) ... leaving\n", whatever);
	exit(0);
}

void
change_user(Spuser *user)
{
	sp_change_user(user);
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

	create_file(root, "ctl", 0644, Qctl, &ctl_ops, NULL, NULL);
	datafile = create_file(root, "data", 0444, Qdata, &data_ops, NULL, NULL);
	datafile->length = filelen;
	create_file(root, "avail", 0666, Qavail, &avail_ops, NULL, NULL);
	file = create_file(root, "redir", 0444, Qredir, &redir_ops, NULL, NULL);
	/* make it too long, it matters not ... */
	file->length = 32;
}

static off_t
filesize(char *file)
{
	struct stat buf;

	if (stat(file, &buf) < 0)
		return -1;
	return buf.st_size;
}

static char *
dothefile(char *filename)
{
	int fd;

	filelen = filesize(filename);

	if (filelen == -1) {
		perror("no file?");
		exit(1);
	}
	data = malloc(filelen);
	if (! data){
		perror("malloc");
		exit(1);
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0){
		perror("open");
		exit(1);
	}
	if (read(fd, data, filelen) < filelen){
		perror("read");
		exit(1);
	}
	return filename;
}

int
netfile(Spcfsys *Client, char *name, char **data)
{
	Spwstat *spc_stat(Spcfsys *fs, char *path);
	Spwstat *stat;
	u64 len;
	Spcfid *file;

	stat = spc_stat(Client, name);
	if (!stat){
		debug(1, "Stat of :%s: fails\n", name);
		return -1;
	}
	len = stat->length;

	file = spc_open(Client, name, Oread);
	if (!file){
		debug(1, "Open of :%s: fails\n", name);
		return -1;
	}

	*data = malloc(len);
	if (!*data)
		return -1;
	memset(*data, 0, len);

	if ((len = spc_read(file, (u8 *) *data, len, 0)) < 0){
		debug(1, "Read of :%s: fails\n", name);
		return -1;
	}

	return len;
}

void server()
{

}

Spcfsys * client()
{
	int redirlen, worker = 0;
	char *action;
	char *dataaddress;
	int ecode;
	char *ename;
	Spcfsys *Client = NULL;

	Client = spc_netmount(netaddress, user, defport, NULL, NULL);
	if (!Client)
		goto error;
	redirlen = netfile(Client, "redir", &action);
	if (redirlen <= 0)
		goto error;
	debug(1, "redir reads as :%s:\n", action);
	/* get the data server address */
	if (!strcmp(action, "me"))
		dataaddress = netaddress;
	else if (!strcmp(action, "you")){
		dataaddress = netaddress;
		worker = 1;
	} else {
		char *portname;
		dataaddress = action;
		portname = strchr(action, '!');
		if (! portname)
			goto error;
		defport = strtoul(portname, 0, 0);
		spc_umount(Client);
		debug(1, "Client: mount tcp!%s!%s\n", dataaddress, portname);
		Client = spc_netmount(dataaddress, user, defport, NULL, NULL);
		if (!Client)
			goto error;
	}
	debug(1, "Try to read data, then %s\n", worker ? 
			"become worker" : "exit");
	filelen = netfile(Client, "data", &data);
	if (filelen <= 0)
		goto error;
	debug(1, "Got the data, %d bytes\n", filelen);

	if (!worker) {
		/* we don't become a worker so exit */
		printf("all done, bye\n");
		exit(0);
	}

	return Client;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
	return Client;
}
int
main(int argc, char **argv)
{
	pid_t pid;
	int c, ecode;
	char *s, *ename;
	extern Spuserpool *sp_unix_users;

	while ((c = getopt(argc, argv, "dD:p:f:n:")) != -1) {
		switch (c) {
		case 'd':
			spc_chatty = 1;
			break;
		case 'p':
			defport = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'D':
			debuglevel = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'f':
			if ((file = dothefile(optarg)) == NULL)
				goto error;
			break;
		case 'n':
			netaddress = optarg;
			break;

		default:
			usage(argv[0]);
		}
	}

	if ((!file) && (!netaddress)) {
		fprintf(stderr, "please supply either -f or -n arguments\n");
		usage(argv[0]);
	}

	if (optind != argc)
		usage(argv[0]);

//	read_config();

	up = sp_unix_users;
	user = up->uid2user(up, getuid());
	if (!user) {
		fprintf(stderr, "user not found\n");
		exit(1);
	}

	avail = qalloc();
	if (!avail)
		goto error;

	signal(SIGALRM, timeout);
	if (netaddress) {
		Spcfid *avail;
		Spcfsys *Client;

		if ((Client = client()) == NULL) {
			exit(1);
		}

		avail = spc_open(Client, "avail", Owrite);
		if (!avail)
			goto error;

		//server();

		fsinit();

		defport = 0;
		srv = sp_socksrv_create_tcp(&defport);
		if (!srv)
			goto error;

		/* FixME: re-queue ourself ? */
		while (1) {
			char port[32];
			/* now serve one guy. Do this for a while */
			srv->debuglevel = debuglevel > 1;
			srv->dotu = 1;
			spfile_init_srv(srv, root);
			sp_srv_start(srv);

			/* why is it we don't print our IP here?
			 * because we should not trust a client. We will trust
			 * their port, but their IP is determined by the server
			 */
			sprintf(port, "%d", defport);
			if (spc_write(avail, (u8 *)port, strlen(port), 0) < 0)
				goto error;

			fprintf(stderr, "becoming server on port %d\n",
				defport);
			while (1){
				/* servicetime second window to wait + do request*/
				alarm(servicetime);
				sp_poll_once();
			}
			

			return 0;
		}
	}
		
		
	fsinit();

	srv = sp_socksrv_create_tcp(&defport);
	if (!srv)
		goto error;

	srv->debuglevel = debuglevel > 1;
	srv->dotu = 0;
	spfile_init_srv(srv, root);
	sp_srv_start(srv);

	if (!debuglevel) {
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

	while (1){
		/* servicetime second window to wait + do request*/
		alarm(servicetime);
		sp_poll_once();
	}

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
	return -1;
}

/*static int
ctl_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	return 0;
}*/

static int
ctl_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	char buf[32];

	snprintf(buf, sizeof buf, "%d", 3);
	return cutstr(data, offset, count, buf, 0);
}

static int
redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	char buf[32];
	struct Worker *worker;

	if (offset)
		return 0;

	memset(buf, 0, sizeof(buf));

	/* for redirectors, i.e. non-root, they can only ever say "me" */
	strncpy(buf, "me", sizeof(buf));

	/* but if we're the master ... */
	if (file) {
		if ((worker = recvq(avail))){
			snprintf(buf, sizeof(buf), "%s!%d",
				 worker->ip, worker->port);
			free(worker);
		} else
			strncpy(buf, "you", sizeof(buf));
	}
	
	count = strlen(buf);
	memcpy(data, buf, count);

	return count;
}

static int
data_read(Spfilefid *fid, u64 offset, u32 count, u8 *buf, Spreq *req)
{
	if (offset > filelen)
		return 0;
	if ((offset + count) > filelen)
		count = filelen - offset;
	memcpy(buf, &data[offset], count);
	return count;
}

static int
avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int port;
	struct Worker *worker;

	if (offset)
		return 0;

	if ((port = strtoul((const char *)data, 0, 0)) == 0) {
		sp_werror("invalid port number", EINVAL);
		return -1;
	}

	worker = malloc(sizeof(struct Worker));
	if (!worker) {
		sp_werror("malloc", ENOMEM);
		return -1;
	}

	/* get the ip address of the client from the connection */
	worker->ip = strtok(req->conn->address, "!");
	worker->port = port;

	sendq(avail, worker);

	return count;
}

static int
avail_wstat(Spfile* file, Spstat* stat)
{
	return 1;
}
