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
#include <syslog.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>

#include "npfs.h"
#include "npclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "libxauth.h"
#include "queue.h"
#include "statfs.h"

static void	usage(char *name);
static void	debug(int level, char *fmt, ...);
static void	read_config(void);
static void	*session_connect(void *v);
static void	fsinit(void);
//static int	ctl_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
//static int 	ctl_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
static int	state_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
//static int	notify_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
static Npfile *dir_next(Npfile *dir, Npfile *prevchild);
static Npfile *dir_first(Npfile *dir);

Npdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Npfileops state_ops = {
	.read = state_read,
};

/*Npfileops notify_ops = {
	.read = notify_read,
};
*/

Npuser *user;
Node *node;


Npuser *adminuser;
Npgroup *admingroup;
Xkey *adminkey;

Npsrv 	*srv;
Npfile 	*root;
char 	*defip = "127.0.0.1";
char 	*defproto = "tcp";
char	*service = "tcp!*!20002";
char 	*cfg = "/etc/clustermatic/statfs.conf";
char 	*defstate = "up";	/* this is the default state a newly booted node is set to */
int	defport = 20002;
int	nodetach;
unsigned int	debugmask = 0;

extern int npc_chatty;

static void 
usage(char *name) {
	fprintf(stderr, "usage: %s [-dn] [-D debugmask] [-a adminkey] [-c config]\n", name);
	exit(1);
}

static void
debug(int mask, char *fmt, ...)
{
	va_list arg;

	if (!(debugmask & mask))
		return;
	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);
}

void
change_user(Npuser *user)
{
	np_change_user(user);
}

static Npfile*
dir_first(Npfile *dir)
{
	npfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Npfile*
dir_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static void
read_config(void)
{
	FILE *f;
	Node *nn;
	char buf[Bufsize];
	char *l, *c, *t;
	int err;

	debug(Dbgfn, "confinit...\n");
	f = fopen(cfg, "r");
	if(f == NULL) {
		fprintf(stderr, "can not fopen %s\n", cfg);
		exit(1);
	}

	nn = node;
	while((l = fgets(buf, Bufsize, f)) != NULL) {
		if(l[strlen(l)-1] == '\n')
			l[strlen(l)-1] = '\0';

		if((c = strchr(l, '=')) != NULL) {
			*c++ = '\0';
			debug(Dbgcfg, "config: %s=%s\n", l, c);
			if(node != NULL) {
				nn->next = malloc(sizeof(Node));
				if(nn->next == NULL) {
					fprintf(stderr, "out of memory (1)\n");
					exit(1);
				}
				nn = nn->next;
			} else {
				nn = malloc(sizeof(Node));
				if(nn == NULL) {
					fprintf(stderr, "out of memory (2)\n");
					exit(1);
				}
				node = nn;
			}
			nn->name = strdup(l);
			nn->addr = strdup(c);
			nn->ip = strdup(c);
			if((t = strrchr(nn->ip, '!')) != NULL) {
				*t = '\0';
				if((t = strrchr(nn->ip, '!')) != NULL) {
					nn->ip = t+1;
				}
			} 

			nn->status = NULL;
			nn->arch = NULL;
			debug(Dbgcfg, "node: name: %s, addr: %s, ip: %s\n", nn->name, nn->addr, nn->ip);
			while((err = pthread_mutex_init(&nn->mux, NULL)) < 0) {
				if(err != EAGAIN) {
					perror("error creating mutex");
					exit(1);
				}
			}
		} else {
			debug(Dbgcfg, "config, illegal: %s\n", l);
		}
	}
	fclose(f);
}

static Npfile *
create_file(Npfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Npuser *usr, void *aux)
{
	Npfile *ret;

	ret = npfile_alloc(parent, name, mode, qpath, ops, aux);
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
	npfile_incref(ret);
	return ret;
}

static void
fsinit(void)	
{		   
	root = npfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	npfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;
			
	create_file(root, "state", 0444, Qstats, &state_ops, NULL, NULL);
//	create_file(root, "notify", 0444, Qnotify, &notify_ops, NULL, NULL);
}


static void *
session_connect(void *v)
{
	Node *n = v;
	unsigned char buf[512];
	int err = 0, i;
	char *arch = NULL;

	debug(Dbgfn, "session_connect\n");

	
	while(1) {
		sleep(5);
		debug(Dbgfn, "main loop: %ld\n", n);

		/* locking needs to be thought-out better. this is too excessive */

		if(n->fs == NULL) {
//			n->fs = npc_netmount(n->addr, user->uname, n->port);
			n->fs = xp_node_mount(n->addr, user, adminkey);
			if (!n->fs) {
				err = 1;
				n->ename = "netmount";
				goto conn_error;
			}
		}

		/* we only update n->arch if we've lost connection */
		if(arch == NULL) {
			n->fid = npc_open(n->fs, "arch", Oread);
			if (!n->fid) {
				err = 1;
				n->ename = "open arch";
				goto conn_error;
			}
			if((i = npc_read(n->fid, buf, 511, 0)) < 0) {
				err = 1;
				n->ename = "read arch";
				goto conn_error;
			} else {
				buf[i] = '\0';	
				if(arch)
					free(arch);
				arch = strdup((char *)buf);
			}
		}

		if(n->fid)
			npc_close(n->fid);
		n->fid = npc_open(n->fs, "state", Ordwr);
		if (!n->fid) {
			err = 1;
			n->ename = "open state";
			goto conn_error;
		}
		if((i = npc_read(n->fid, buf, 511, 0)) < 0) {
			err = 1;
			n->ename = "read state";
			goto conn_error;
		} else if(i == 0) {
			/* if state is nil we set it to up */
			if(npc_write(n->fid, (unsigned char *)"up", 2, 0) != 2) {
				err = 1;
				n->ename = "write state";
				goto conn_error;
			} else {
				strcpy((char *)buf, "up");
			}
		} 
		pthread_mutex_lock(&n->mux);
		buf[i] = '\0';
		if(n->status)
			free(n->status);
		n->status = strdup((char *)buf);
		if(n->arch)
			free(n->arch);
		n->arch = strdup(arch);

conn_error:
		pthread_mutex_unlock(&n->mux);
		if(err) {
			if(n->fid) {
				npc_close(n->fid);
				n->fid = NULL;
			}
			if(n->fs) {
				npc_umount(n->fs);
				n->fs = NULL;
			}
			if(n->arch) {
				free(n->arch);
				n->arch = NULL;
			}
			if(n->status) {
				free(n->status);
				n->status = NULL;
			}
			if(err)	{
				syslog(LOG_USER|LOG_ERR, "statfs: %s: %s: %d\n", n->name, n->ename, n->ecode);
				fprintf(stderr, "statfs: %s: %s: %d\n", n->name, n->ename, n->ecode);
			}
			err = 0;
		}
	}
}

int
main(int argc, char **argv)
{
	Node *nn;
	int c, ecode, nwthreads = 16, pid;
	char *s, *ename;
	pthread_attr_t attr;
	size_t stacksize;
	int threadcount = 0;
	Npuserpool *upool;
	char *afname = "/etc/xcpu/admin_key";
	struct passwd *xcpu_admin;
	gid_t xcpu_gid;

	while ((c = getopt(argc, argv, "ndD:p:c:w:a:")) != -1) {
		switch (c) {
		case 'n':
			nodetach++;
			break;
		case 'a':
			afname = optarg;
			break;
		case 'c':
			cfg = optarg;
			break;
		case 'd':
			npc_chatty = 1;
			break;
		case 'p':
			defport = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'D':
			debugmask = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);
			break;
		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if(*s != '\0')
				usage(argv[0]);

		default:
			usage(argv[0]);
		}
	}

	if (optind != argc)
		usage(argv[0]);

	read_config();

	upool = np_priv_userpool_create();
	if (!upool) {
		fprintf(stderr, "can not create user pool\n");
		exit(1);
	}

	adminkey = xauth_privkey_create(afname);
	if (!adminkey) {
		adminkey = (Xkey *) ~0;
		np_werror(NULL, 0);
	}

	xcpu_admin = getpwnam("xcpu-admin");
	if (xcpu_admin)
		xcpu_gid = xcpu_admin->pw_gid;
	else
		xcpu_gid = 65530;
	adminuser = np_priv_user_add(upool, "xcpu-admin", xcpu_gid, adminkey);
	if (!adminuser)
		goto error;
	user = adminuser;

	admingroup = np_priv_group_add(upool, "xcpu-admin", -3);
	if (!admingroup)
		goto error;

	if(! nodetach) {
		switch(pid = fork()) {;
		case -1:
			perror("cannot fork");
			exit(1);
		case 0:
			/* child */
			close(0);
			open("/dev/null", O_RDONLY);
			close(1);
			open("/dev/null", O_WRONLY);
			close(2);
			open("/dev/null", O_WRONLY);

			setsid();
			chdir("/");
			break;
		default:
			/* parent */
			exit(0);
		}
	}


	nn = node;

	/* make a reasonable default stack size. Most Linux default to 2M! */
	if (pthread_attr_init(&attr)) {
		perror("pthread_attr_init");
		exit(1);
	}
	if (pthread_attr_getstacksize(&attr, &stacksize)) {
		perror("getstacksize");
		exit(1);
	}
	debug(Dbgcfg, "default stacksize is %lx\n", stacksize);

	stacksize = 8192;
	if (pthread_attr_setstacksize(&attr, stacksize))
		perror("set stack size");

	while(nn != NULL) {
		debug(Dbgcfg, "creating thread #%d for %s@%s\n", threadcount++, nn->name, nn->ip);
		if (pthread_create(&nn->thread, NULL, session_connect, nn))
			perror("Pthread create main per-server child thread");
		nn = nn->next;
	}

	fsinit();
	srv = np_socksrv_create_tcp(nwthreads, &defport);
	if (!srv)
		goto error;

	npfile_init_srv(srv, root);
	srv->debuglevel = debugmask;
	srv->dotu = 1;
	srv->upool = upool;
	np_srv_start(srv);

	syslog(LOG_USER|LOG_NOTICE, "statfs: started...");
	fprintf(stderr, "statfs: started...");

	while(1)
		sleep(1000);

error:
	np_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
	return -1;

}

static int
state_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Node *n;
	char buf[8192], tbuf[512];

			
	buf[0] = '\0';
	for(n = node; n; n = n->next) {
		if(strlen(buf) < count) {
			pthread_mutex_lock(&n->mux);
			snprintf(tbuf, sizeof tbuf, "%s\t%s\t%s\t%s\n", n->name, 
					n->ip,
					(n->arch && n->arch[0] != '\0') ? n->arch : "unknown", 
					(n->status && n->status[0] != '\0') ? n->status : "down");
			pthread_mutex_unlock(&n->mux);
			strncat(buf, tbuf, sizeof buf);
		}
	}
	return cutstr(data, offset, count, buf, 0);
}
