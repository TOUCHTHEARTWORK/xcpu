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

char *qemupath = "/usr/bin/qemu";

int
dev_create_qemu(Xsession *xs, char *devname, char *devimage, int boot)
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

int
net_create_qemu(Xsession *xs, char *id, char *mac)
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
monitor_command_qemu(Xsession *xs, char *cmd)
{
	int n;
	char buf[1024];

	if (xs->monin < 0) {
		sp_werror("vm not started", EIO);
		return -1;
	}

	n = write(xs->monin, cmd, strlen(cmd));
	if (n < 0) {
		sp_uerror(errno);
		return -1;
	}

	n = read(xs->monout, buf, sizeof(buf));
	if (n < 0) {
		sp_uerror(errno);
		return -1;
	}

	// TODO: check the output

	return 0;
}
int
start_qemu(Xsession *xs, int freeze)
{
	int pid, n, sz, argc;
	int pip[2], monin[2], monout[2];
	char buf[2048], b;
	char *argv[256];
	char *s, *boot;
	Blockdev *bd;
	Netdev *nd;
	Xsession *ss;

	boot = NULL;
	s = buf;
	sz = sizeof(buf);
	n = 0;
	argc = 0;

	argv[argc++] = qemupath;
	argv[argc++] = "-monitor";
	argv[argc++] = "stdio";
	argv[argc++] = "-nographic";
	argv[argc++] = "-serial";
	argv[argc++] = "null";
	argv[argc++] = "-m";
	argv[argc++] = s + n;
	n += snprintf(s+n, sz-n, "%d", xs->memsize) + 1;
	if (freeze)
		argv[argc++] = "-S";

	for(bd = xs->bdevs; bd != NULL; bd = bd->next) {
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "-%s", bd->devname) + 1;
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "%s", bd->devimage) + 1;

		if (bd->boot)
			boot = bd->devname;
	}
	if (boot) {
		argv[argc++] = "-boot";
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "%c", boot[2]) + 1;	// kludge
	}

	for(nd = xs->ndevs; nd != NULL; nd = nd->next) {
		argv[argc++] = "-net";
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "nic,vlan=%s,macaddr=%s", nd->id, nd->mac) + 1;
		argv[argc++] = "-net";
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "tap,vlan=%s ", nd->id) + 1;
	}

	if (xs->vmimage) {
		argv[argc++] = "-loadvm";
		argv[argc++] = s + n;
		n += snprintf(s+n, sz-n, "%s ", xs->vmimage) + 1;
	}

	argv[argc] = NULL;
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
//		for(i = 0; argv[i] != NULL; i++)
//			fprintf(stderr, "%s ", argv[i]);
//
//		fprintf(stderr, "\n");
		execv(qemupath, argv);
child_error:
		perror("error");
		exit(errno);
	}

	/* parent */
	xs->state = Running;
	xs->monin = monin[1];
	xs->monout = monout[0];
	session_incref(xs);

	close(monin[0]);
	close(monout[1]);
	xs->pid = pid;
	close(pip[0]);
	b = 5;
	write(pip[1], &b, 1);
	close(pip[1]);
	return 1;
}

int
execute_command_qemu(Xsession *xs, char *s)
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

		ret = dev_create_qemu(xs, args[0], args[1], boot)==-1?0:1;
	} else if (strcmp("net", cmd) == 0) {
		if (nargs != 3) {
			sp_werror("invalid number of arguments", EIO);
			goto done;
		}

		ret = net_create_qemu(xs, args[0], args[1])?0:1;
	} else if (strcmp("loadvm", cmd) == 0) {
		if (xs->state == Running) {
			snprintf(buf, sizeof(buf), "loadvm %s\n", args[0]);
			ret = monitor_command_qemu(xs, buf)==-1?0:1;
		} else {
			xs->vmimage = strdup(args[0]);
			ret = 1;
		}
	} else if (strcmp("storevm", cmd) == 0) {
		snprintf(buf, sizeof(buf), "savevm %s\n", args[0]);
		ret = monitor_command_qemu(xs, buf)==-1?0:1;
	} else if (strcmp("power", cmd) == 0) {
		if (strcmp(args[0], "on") == 0) {
			if (args[1] && strcmp(args[1], "freeze") == 0) 
				n = 1;
			else 
				n = 0;

			ret = start_qemu(xs, n)==-1?0:1;
		} else
			ret = monitor_command_qemu(xs, "quit\n")==-1?0:1;
	} else if (strcmp("freeze", cmd) == 0) {
		ret = monitor_command_qemu(xs, "stop\n")==-1?0:1;
	} else if (strcmp("unfreeze", cmd) == 0) {
		ret = monitor_command_qemu(xs, "cont\n")==-1?0:1;
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
	} else if (strcmp("redir", cmd) == 0) {
		sp_werror("not implemented", EIO);
	} else 
		sp_werror("invalid command", EIO);
	


done:
	free(toks);
	return ret;

}
