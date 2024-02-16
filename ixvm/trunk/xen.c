#include <stdlib.h>
#include <stdio.h>
/* what a kludge! see man 2 pread */
#define _XOPEN_SOURCE 500
/* and yuck it doesn't work anyway. */
#include <unistd.h>
#include <string.h>
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
#include <time.h>
#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "ixvm.h"

char *xmpath = "/usr/sbin/xm";

static int
dev_create_xen(Xsession *xs, char *devname, char *devimage, int boot)
{
	Blockdev *bd;

	fprintf(stderr, "dev create\n");
	// TODO: check if devname is valid
	bd = sp_malloc(sizeof(*bd));
	if (!bd)
		return -1;

	bd->devname = strdup(devname);
	bd->devimage = strdup(devimage);
	bd->boot = boot;
	bd->next = xs->bdevs;
	xs->bdevs = bd;

	return 0;
}

static int
net_create_xen(Xsession *xs, char *id, char *mac)
{
	Netdev *nd;

	fprintf(stderr, "net create\n");
	// TODO: check if we already have netdev with that id ...
	nd = sp_malloc(sizeof(*nd));
	if (!nd)
		return -1;

	nd->id = strdup(id);
	nd->mac = strdup(mac);
	nd->next = xs->ndevs;
	xs->ndevs = nd;

	return 0;
}

int
xm_command(Xsession *xs, char *cmd)
{
	int pid, n, sz, argc, nargs, i;
	int pip[2], monin[2], monout[2];
	char buf[2048], b;
	char *argv[256];
	char *s, *boot;
	char **toks;
	//Blockdev *bd;
	//Netdev *nd;
	Xsession *ss;

	boot = NULL;
	s = buf;
	sz = sizeof(buf);
	n = 0;
	argc = 0;

	nargs = tokenize(s, &toks);
	if (nargs < 0) {
		sp_werror("invalid format", EIO);
		return 0;
	}
	argv[argc++] = xmpath;
	for(i = 1; i < nargs; i++)
		argv[argc++] = toks[i];
	argv[argc] = NULL;
	free(toks);

	if (pipe(pip) < 0) {
		sp_uerror(errno);
		return 0;
	}

	if (pipe(monin) < 0) {
		sp_uerror(errno);
		return 0;
	}

	if (pipe(monout) < 0) {
		sp_uerror(errno);
		return 0;
	}

	pid = fork();
	if (pid == -1) {
		sp_werror("cannot fork", errno);
		return 0;
	} else if (pid == 0) {
		/* child */
		/* close the file descriptors for all other sessions */
		for(ss = sessions; ss != NULL; ss = ss->next) {
			if (xs == ss)
				continue;

			close(ss->monin);
			close(ss->monout);
		}

		if (dup2(monin[0], 0) < 0)
			goto child_error;

		if (dup2(monout[1], 1) < 0)
			goto child_error;

//		if (dup2(monout[1], 2) < 0)
//			goto child_error;

		close(monin[1]);
		close(monout[0]);
		close(pip[1]);
		read(pip[0], &b, 1);
		close(pip[0]);
		chdir(xs->dirpath);
		execv(xmpath, argv);
child_error:
		perror("error");
		exit(errno);
	}

	close(monin[0]);
	close(monout[1]);
	close(pip[0]);
	b = 5;
	write(pip[1], &b, 1);
	close(pip[1]);

	while((n = read(monout[0], buf, sizeof(buf))) > 0) {
		// TODO: fill the response buffer with command output
	}
	if(n < 0) {
		sp_uerror(errno);
		return -1;
	}

	// TODO: check the output

	return 0;
}
int
start_xen(Xsession *xs, int freeze)
{
	char buf[2048];

	/* TODO: parse config file to get VM name, for now we require it to be
	 * set by the script that created the VM
	 */

	if(xs->cfg == NULL) {
		sp_werror("no config file for vm", EIO);
		return 0;
	}

	if(xs->vmname == NULL) {
		sp_werror("no vm name. please issue a 'vmname' command", EIO);
		return 0;
	}

	snprintf(buf, sizeof(buf), "create %s", xs->cfg);
	xm_command(xs, buf);
	return 1;
}

int
execute_command_xen(Xsession *xs, char *s)
{
	int n, ret, nargs, boot;
	char buf[128];
	char *t;
	char **toks, *cmd, **args;

	nargs = tokenize(s, &toks);
	if (nargs < 0) {
		sp_werror("invalid format", EIO);
		return 0;
	}

	cmd = toks[0];
	args = &toks[1];

	ret = 0;
	if (strcmp("dev", cmd) == 0) {
		if (nargs>4 || nargs<3) {
			sp_werror("invalid number of arguments", EIO);
			goto done;
		}

		n = 0;
		if (nargs==4 && strcmp(args[2], "boot")==0)
			boot = 1;

		ret = dev_create_xen(xs, args[0], args[1], boot)==-1?0:1;
	} else if (strcmp("net", cmd) == 0) {
		if (nargs != 3) {
			sp_werror("invalid number of arguments", EIO);
			goto done;
		}

		ret = net_create_xen(xs, args[0], args[1])?0:1;

	} else if (strcmp("loadvm", cmd) == 0) {
		if (xs->state == Running) {
			snprintf(buf, sizeof(buf), "loadvm %s\n", args[0]);
			ret = xm_command(xs, buf)==-1?0:1;
		} else {
			xs->vmimage = strdup(args[0]);
			ret = 1;
		}
	} else if (strncmp("vmname", cmd, 3) == 0) {
		if(nargs != 2) {
			sp_werror("vmname: missing config file", EIO);
			goto done;
		}
		if(xs->cfg)
			free(xs->cfg);
		xs->cfg = strdup(args[0]);
		ret = 1;
	} else if (strncmp("cfg", cmd, 3) == 0) {
		if(nargs != 2) {
			sp_werror("cfg: missing config file", EIO);
			goto done;
		}
		if(xs->cfg)
			free(xs->cfg);
		xs->cfg = strdup(args[0]);
		ret = 1;
	} else if (strcmp("storevm", cmd) == 0) {
		snprintf(buf, sizeof(buf), "savevm %s\n", args[0]);
		ret = xm_command(xs, buf)==-1?0:1;
	} else if (strcmp("power", cmd) == 0) {
		if (strcmp(args[0], "on") == 0) {
			if (args[1] && strcmp(args[1], "freeze") == 0) 
				n = 1;
			else 
				n = 0;

			ret = start_xen(xs, n)==-1?0:1;
		} else
			ret = xm_command(xs, "quit\n")==-1?0:1;
	} else if (strcmp("freeze", cmd) == 0) {
		ret = xm_command(xs, "stop\n")==-1?0:1;
	} else if (strcmp("unfreeze", cmd) == 0) {
		ret = xm_command(xs, "cont\n")==-1?0:1;
	} else if (strcmp("emigrate", cmd) == 0) {
		sp_werror("not implemented", EIO);
	} else if (strcmp("immigrate", cmd) == 0) {
		sp_werror("not implemented", EIO);
	} else if (strcmp("clone", cmd) == 0) {
		if (!args[0] || !args[1]) {
			sp_werror("too few arguments", EIO);
			goto done;
		}

		n = strtol(args[0], &t, 10);
		if (*t != '\0') {
			sp_werror("invalid maxsession format", EIO);
			goto done;
		}

		tspawn(xs, n, args[1]);
		ret = -1;
	} else if (strcmp("cpus", cmd) == 0) {
		if(nargs != 2) {
			sp_werror("expected argument", EIO);
			goto done;
		}
		snprintf(buf, sizeof(buf), "vcpu-set %s %s", args[0], xs->vmname);
		xm_command(xs, buf);
		xs->cpus = atoi(args[0]);
	} else if (strcmp("memsize", cmd) == 0) {
		if(nargs != 2) {
			sp_werror("expected argument", EIO);
			goto done;
		}
		snprintf(buf, sizeof(buf), "mem-set %s %s", args[0], xs->vmname);
		xm_command(xs, buf);
	} else if (strcmp("redir", cmd) == 0) {
		sp_werror("not implemented", EIO);
	} else 
		sp_werror("invalid command", EIO);
	
done:
	free(toks);
	return ret;
}
