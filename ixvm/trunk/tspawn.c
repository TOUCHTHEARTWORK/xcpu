//#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
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
#include <time.h>
#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "ixvm.h"

typedef struct Node Node;

struct Node {
	Tspawn*		tspawn;
	char*		addr;
	char*		sid;
	Spcfsys*	fs;
	Rxfile*		files;
	Rxcopy*		copyf;
};

struct Tspawn {
	Xsession*	xs;
	Spreq*		req;
	int		maxsessions;
	int		nnodes;
	Node**		nodes;
	int		ndone;
};

static void node_cb(void *cba);
static void node_destroy(Node *nd);

static int
getnodes(char *nodelist, char ***nodes)
{
	int i, n;
	char *s, **ns;

	*nodes = NULL;
	n = 1;

	s = nodelist + (strlen(nodelist) - 1);
	while (*s == ',')
		s--;
	*(s+1) = '\0';

	s = nodelist;
	while ((s = strchr(s, ',')) != NULL) {
		n++;
		s++;
	}

	ns = malloc(n * sizeof(char *) + strlen(nodelist) + 1);
	if (!ns) {
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	s = (char *) ns + n*sizeof(char *);
	strcpy(s, nodelist);
	for(i = 0; i < n; i++) {
		ns[i] = s;
		s = strchr(s, ',');
		if (!s)
			break;

		*s = '\0';
		s++;
	}

	*nodes = ns;
	return n;
}

Node *
node_create(Tspawn *ts, char *addr)
{
	char *s;
	Spuser *user;
	Node *nd;
	Rxfile *first, *last;

	first = NULL;
	last = NULL;
	nd = malloc(sizeof(*nd));
	if (!nd) {
		sp_werror(Enomem, ENOMEM);
		return NULL;
	}

	s = strchr(addr, '/');
	if (!s) {
		sp_werror("invalid address format", EIO);
		free(s);
		return NULL;
	}

	*s = '\0';
	s++;

	nd->tspawn = ts;
	nd->addr = strdup(addr);
	nd->sid = strdup(s);
	nd->fs = NULL;
	nd->files = NULL;

	user = sp_unix_users->uid2user(sp_unix_users, geteuid());
	if (!user) {
		sp_werror("user not found", EIO);
		goto error;
	}

	nd->fs = spc_netmount(addr, user, XCPU_PORT, NULL, NULL);
	if (!nd->fs)
		goto error;

	return nd;

error:
	if (nd->fs)
		spc_umount(nd->fs);
	free(nd->addr);
	free(nd->sid);
	free(nd);
	return NULL;
}

int
node_setup(Node *nd, Xsession *xs, int maxsessions, int nclones, char **clones)
{
	int i, n;
	char buf[256], *t;
	Rxfile *first, *last, *f;

	first = NULL;
	last = NULL;

	if (!clones)
		nclones = 0;

	snprintf(buf, sizeof(buf), "%s/fs", nd->sid);
	f = rxfile_create_from_file(nd->fs, buf, xs->dirpath);
	if (!f)
		goto error;

	if (!first)
		first = f;
	if (last)
		last->next = f;

	while (f->next != NULL)
		f = f->next;
	last = f;

	if (nclones) {
		n = 64;
		for(i = 0; i < nclones; i++)
			n += strlen(clones[i]) + 1;

		t = malloc(n + 2);
		if (!t) {
			sp_werror(Enomem, ENOMEM);
			goto error;
		}

		sprintf(t, "clone %d ", maxsessions);
		for(i = 0; i < nclones; i++) {
			strcat(t, clones[i]);
			strcat(t, ",");
		}
		strcat(t, "\n");

		snprintf(buf, sizeof(buf), "%s/ctl", nd->sid);
		f = rxfile_create_from_buf(nd->fs, buf, t, strlen(t));
		if (!f)
			goto error;

		if (!first)
			first = f;
		if (last)
			last->next = f;
		last = f;
	} 

	nd->files = first;
	nd->copyf = rxfile_copy_start(nd->files, node_cb, nd);
	return 0;

error:
	rxfile_destroy_all(first);
	return -1;
}

static void
node_cb(void *cba)
{
	int i;
	Node *nd;
	Tspawn *ts;
	Xsession *xs;

	nd = cba;
	ts = nd->tspawn;
	xs = ts->xs;

	ts->ndone++;
	if (ts->ndone < ts->nnodes)
		return;

	for(i = 0; i < ts->nnodes; i++)
		node_destroy(ts->nodes[i]);

	free(ts->nodes);
	ctl_execute_commands(xs);
	free(ts);
}

static void
node_destroy(Node *nd)
{
	free(nd->addr);
	free(nd->sid);

	if (nd->copyf)
		rxfile_copy_finish(nd->copyf);

	if (nd->files)
		rxfile_destroy_all(nd->files);

	if (nd->fs)
		spc_umount(nd->fs);

	free(nd);
}

int
tspawn(Xsession *xs, int maxsessions, char *dest)
{
	int i, m, n, ns, b, e, cn, ce, idx;
	char **nds, **p;
	Tspawn *ts;

	ts = malloc(sizeof(*ts));
	if (!ts) {
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	ns = getnodes(dest, &nds);
	if (ns < 0) {
		free(ts);
		return -1;
	}

	if (maxsessions > 0) {
		n = maxsessions;
		if (n > ns)
			n = ns;
		m = ns/n + (ns%n?1:0);
	} else {
		n = ns;
		m = 1;
	}

	ts->xs = xs;
	ts->maxsessions = maxsessions;
	ts->nnodes = n;
	ts->nodes = calloc(ts->nnodes, sizeof(Node *));
	if (!ts->nodes) {
		free(ts);
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	if (m > 1)
		cn = (ns-n)/(m-1) + ((ns-n)%(m-1)?1:0);
	else
		cn = 0;

	ce = ns - n + cn;
	ts->ndone = 0;

	i = 0;
	idx = 0;
	while (i < ce) {
		b = i + 1;
		e = i + m;
		if (e > ce)
			e = ce;

		if (b < e)
			p = &nds[b];
		else 
			p = NULL;
			
		ts->nodes[idx] = node_create(ts, nds[i]);
		if (!ts->nodes[idx])
			goto error;

		if (node_setup(ts->nodes[idx], xs, maxsessions, e - b, p) < 0)
			goto error;

		i = e;
		idx++;
	}

	while (i < ns) {
		ts->nodes[idx] = node_create(ts, nds[i]);
		if (!ts->nodes[idx])
			goto error;

		if (node_setup(ts->nodes[idx], xs, maxsessions, 0, NULL) < 0)
			goto error;

		i++;
		idx++;
	}

	free(nds);
	return 0;

error:
	for(i = 0; i < ts->nnodes; i++)
		if (ts->nodes[i])
			node_destroy(ts->nodes[i]);

	free(nds);
	free(ts->nodes);
	free(ts);
	return -1;
}
