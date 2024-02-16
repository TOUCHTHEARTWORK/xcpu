//#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "npfs.h"
#include "npclient.h"
#include "xcpu.h"
#include "xcpufs.h"
#include "copyfile.h"

#define Bufsize 65536

static int
mount_session(Cnpfile *f, char *rs, char *uname)
{
	char *dest, *sid;

	dest = strdup(rs);
	if (!dest) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	sid = strchr(dest, '/');
	if (!sid) {
		np_werror("invalid remote session", EIO);
		free(dest);
		return -1;
	}
	*(sid++) = '\0';

	f->name = malloc(strlen(sid) + 16);
	if (!f->name) {
		np_werror(Enomem, ENOMEM);
		free(dest);
		return -1;
	}

	sprintf(f->name, "%s/", sid);
	f->fs = npc_netmount(dest, uname, XCPU_PORT);
	if (!f->fs) {
		free(dest);
		free(f->name);
		return -1;
	}

	free(dest);
	return 0;
}

int
clone_session(Xsession *xs, int nsessions, char **rss, char **ctls)
{
	int i, ret;
	int *lens;
	char *buf, **fe;
	Cnpfile *sfiles;

	ret = -1;
	buf = NULL;
	fe = NULL;
	lens = NULL;
	sfiles = malloc(nsessions * sizeof(*sfiles));
	if (!sfiles) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	fe = malloc(nsessions * sizeof(*fe));
	if (!fe) {
		np_werror(Enomem, ENOMEM);
		free(sfiles);
		return -1;
	}

	memset(sfiles, 0, nsessions * sizeof(*sfiles));
	for(i = 0; i < nsessions; i++) {
		if (mount_session(&sfiles[i], rss[i], xs->file->uid->uname) < 0)
			goto done;
	}

	/* remember the end of the sids */
	for(i = 0; i < nsessions; i++)
		fe[i] = sfiles[i].name + strlen(sfiles[i].name);

	/* copy argv */
	for(i = 0; i < nsessions; i++)
		strcpy(fe[i], "argv");

	if (copy_buf2npcfiles(xs->argv.buf, xs->argv.size, nsessions, sfiles, 0666) < 0)
		goto done;

	/* copy env */
	for(i = 0; i < nsessions; i++)
		strcpy(fe[i], "env");

	if (copy_buf2npcfiles(xs->env.buf, xs->env.size, nsessions, sfiles, 0666) < 0)
		goto done;

	/* copy fs */
	buf = malloc(strlen(xs->dirpath) + 16);
	if (!buf) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	for(i = 0; i < nsessions; i++)
		strcpy(fe[i], "fs");

	strcpy(buf, xs->dirpath);
//	sprintf(buf, "%s", xs->dirpath);
	if (copy_file2npcfiles(buf, nsessions, sfiles, 8192) < 0)
		goto done;

	/* clone further */
	if (ctls) {
		lens = malloc(nsessions * sizeof(int));
		if (lens < 0) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		for(i = 0; i < nsessions; i++)
			if (ctls[i])
				lens[i] = strlen(ctls[i]);
			else
				lens[i] = 0;
	}

	for(i = 0; i < nsessions; i++)
		strcpy(fe[i], "ctl");

	copy_bufs2npcfiles(nsessions, ctls, lens, sfiles, 0666);
	ret = 0;

done:
	free(buf);
	free(fe);
	for(i = 0; i < nsessions; i++)
		free(sfiles[i].name);
	free(sfiles);
	free(lens);
	return ret;
		
}

int
getsessions(char *s, char ***sessions)
{
	int n;
	char **ret, *t, *p;

	t = s;
	n = 1;
	while ((t = strchr(t, ',')) != NULL) {
		t++;
		n++;
	}

	ret = malloc(n * sizeof(char *) + strlen(s) + 1);
	if (!ret) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	t = (char *) ret + n * sizeof(char *);
	strcpy(t, s);

	n = 0;
	p = t;
	while (1) {
		ret[n++] = p;
		p = strchr(p, ',');
		if (!p)
			break;

		*p = '\0';
		p++;
	}

	*sessions = ret;
	return n;
}

int 
tspawn(Xsession *xs, int maxsessions, char *dest)
{
	int i, j, k, m, n, ns, ret;
	char **rss, **ctls;
	char **ss;

	n = 0;
	ret = -1;
	rss = NULL;
	ctls = NULL;
	ss = NULL;
	ns = getsessions(dest, &ss);
	if (ns < 0) 
		goto done;

	if (maxsessions > 0) {
		n = maxsessions;
		if (n > ns)
			n = ns;
		m = ns/n + (ns%n?1:0);
	} else {
		n = ns;
		m = 1;
	}

	rss = malloc(n * sizeof(char *));
	if (!rss) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	ctls = malloc(n * sizeof(char *));
	if (!ctls) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}
	memset(ctls, 0, n * sizeof(char *));

	for(i = 0; i < n; i++) {
		rss[i] = ss[i*m];

		k = 0;
		for(j = 1; j<m && i*m+j<ns; j++)
			k += strlen(ss[i*m + j]) + 1;
		ctls[i] = malloc(k + 32);
		if (!ctls[i]) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		sprintf(ctls[i], "clone %d ", maxsessions);
		k = strlen(ctls[i]);
		for(j = 1; j<m && i*m+j<ns; j++) {
			strcat(ctls[i], ss[i*m + j]);
			strcat(ctls[i], ",");
		}

		if (strlen(ctls[i]) != k) 
			ctls[i][strlen(ctls[i]) - 1] = '\n';
		else 
			*ctls[i] = '\0';
	}

	ret = clone_session(xs, n, rss, ctls);

done:
	free(ss);
	free(rss);
	if (ctls) {
		for(i = 0; i < n; i++)
			free(ctls[i]);
		free(ctls);
	}

	return ret;
}
