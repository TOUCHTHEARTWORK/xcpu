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
#include "libxauth.h"
#include "xcpufs.h"

typedef struct Node Node;

struct Node {
	Tspawn*		tspawn;
	char*		addr;
	char*		sid;
	char*		passkey;
	Spcfsys*	fs;
	Rxfile*		files;
	Rxcopy*		copyf;
};

struct Tspawn {
	Xsession*	xs;
	Spuser*		user;
	Spreq*		req;
	int		maxsessions;
	int		nnodes;
	Node**		nodes;
	int		ndone;
};

static void node_cb(void *cba);
static void node_destroy(Node *nd);
static int tspawn_auth(Spcfid *afid, Spuser *user, void *aux);

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
	char *sid, *pkey;
	Node *nd;
	Rxfile *first, *last;

	first = NULL;
	last = NULL;
	nd = malloc(sizeof(*nd));
	if (!nd) {
		sp_werror(Enomem, ENOMEM);
		return NULL;
	}

	sid = strchr(addr, '/');
	if (!sid) {
		sp_werror("invalid address format", EIO);
		return NULL;
	}
	*(sid++) = '\0';

	pkey = strchr(sid, '/');
	if (pkey)
		*(pkey++) = '\0';

	nd->tspawn = ts;
	nd->addr = strdup(addr);
	nd->sid = strdup(sid);
	nd->passkey = pkey?strdup(pkey):NULL;
	nd->fs = NULL;
	nd->files = NULL;

	nd->fs = spc_netmount(addr, ts->user, XCPU_PORT, tspawn_auth, nd);
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

static int
tspawn_auth(Spcfid *afid, Spuser *user, void *aux)
{
	int n;
	char buf[1024], hash[64];
	Node *nd;

	nd = aux;
	if (!nd->passkey)
		return 0;

	n = spc_read(afid, (u8 *) buf, sizeof(buf), 0);
	if (n < 0)
		return -1;

	memmove(buf + n, nd->passkey, strlen(nd->passkey));
	n = xauth_hash((u8 *) buf, n + strlen(nd->passkey), (u8 *) hash, sizeof(hash));
	if (n < 0)
		return -1;

	n = spc_write(afid, (u8 *) hash, n, 0);
	if (n < 0) 
		return -1;

	return 0;
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

	if (xs->env.size) {
		snprintf(buf, sizeof(buf), "%s/env", nd->sid);
		t = malloc(xs->env.size);
		if (!t) {
			sp_werror(Enomem, ENOMEM);
			return -1;
		}
		memmove(t, xs->env.buf, xs->env.size);
		f = rxfile_create_from_buf(nd->fs, buf, t, xs->env.size);
		if (!f)
			goto error;

		if (!first)
			first = f;
		if (last)
			last->next = f;
		last = f;
	}

	if (xs->argv.size) {
		snprintf(buf, sizeof(buf), "%s/argv", nd->sid);
		t = malloc(xs->argv.size);
		if (!t) {
			sp_werror(Enomem, ENOMEM);
			return -1;
		}
		memmove(t, xs->argv.buf, xs->argv.size);
		f = rxfile_create_from_buf(nd->fs, buf, t, xs->argv.size);
		if (!f)
			goto error;

		if (!first)
			first = f;
		if (last)
			last->next = f;
		last = f;
	}

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
	sctl_execute_commands(xs, ts->user);
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
tspawn(Xsession *xs, int maxsessions, char *dest, Spuser *user)
{
	int i, m, n, ns, b, e, cn, ce, idx;
	char **nds, **p;
	Tspawn *ts;

	ts = malloc(sizeof(*ts));
	if (!ts) {
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	ts->user = user;
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
