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

enum {
	Ndestroy	= 1,
	Ndasync		= 2,
};

enum {
	Async		= 1,
};

struct Node {
	int		flags;
	Tspawn*		tspawn;
	char*		addr;
	char*		sid;
	char*		passkey;
	Spcfsys*	fs;
	Rxfile*		files;
	Rxcopy*		copyf;
	char*		ename;
	int		ecode;
};

struct Tspawn {
	int		flags;
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
static void tspawn_done(Tspawn *ts);

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
	nd->ename = NULL;

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
	if (!nd->copyf) {
		nd->files = NULL;
		goto error;
	}

	return 0;

error:
	rxfile_destroy_all(first);
	return -1;
}

static void
node_cb(void *cba)
{
	Node *nd;
	Tspawn *ts;
	Xsession *xs;

	nd = cba;
	ts = nd->tspawn;
	xs = ts->xs;

	ts->ndone++;
	if (sp_haserror()) {
		sp_rerror(&nd->ename, &nd->ecode);
		if (nd->ename)
			nd->ename = strdup(nd->ename);
		sp_werror(NULL, 0);
	}

	/* don't destroy anything even if all the nodes are done
	   before we go out of tspawn() */
	if (ts->ndone<ts->nnodes || !(ts->flags&Async))
		return;

	tspawn_done(ts);
}

static void
node_destroy(Node *nd)
{
	if (!nd)
		return;

	if (nd->flags & Ndestroy)
		return;

	nd->flags |= Ndestroy;
	if (nd->copyf) {
		rxfile_copy_finish(nd->copyf);
		nd->copyf = NULL;
	}

	if (nd->files) {
		rxfile_destroy_all(nd->files);
		nd->files = NULL;
	}

	if (nd->fs) {
		spc_umount(nd->fs);
		nd->fs = NULL;
	}

	free(nd->ename);
	free(nd->addr);
	free(nd->sid);
	free(nd);
}

static void
tspawn_done(Tspawn *ts)
{
	int i, n, m, ecode;
	char ename[512];
	Node *nd;

	/* cleanup the treespawn data and respond to the clone command */
	n = ts->nnodes;
	ts->nnodes = 0;
	ecode = 0;
	ename[0] = 0;
	m = 0;
	for(i = 0; i<n && ts->nodes; i++) {
		nd = ts->nodes[i];
		if (!nd)
			continue;

		if (nd->ecode) {
			if (ecode)
				ecode = nd->ecode;
			m += snprintf(ename+m, sizeof(ename)-m, "%s: %s %d,", nd->addr,
				nd->ename, nd->ecode);
		}
		node_destroy(ts->nodes[i]);
	}

	free(ts->nodes);
	ts->nodes = NULL;
	if (ecode)
		sp_werror(ename, ecode);

	sctl_execute_commands(ts->xs, ts->user);
	ts->xs->ts = NULL;
	free(ts);
}

int
tspawn(Xsession *xs, int maxsessions, char *dest, Spuser *user)
{
	int i, m, n, ns, b, e, cn, ce, idx, ecode;
	char **nds, **p;
	Tspawn *ts;
	char *ename;

	ts = malloc(sizeof(*ts));
	if (!ts) {
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	ts->flags = 0;
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
	xs->ts = ts;
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

	/* check if the treespawn is already done */
	ts->flags |= Async;
	if (ts->ndone >= ts->nnodes)
		tspawn_done(ts);

	free(nds);
	return 0;

error:
	sp_rerror(&ename, &ecode);
	if (ename)
		ename = strdup(ename);


	sp_werror(NULL, 0);
	for(i = 0; i < ts->nnodes; i++)
		if (ts->nodes[i])
			node_destroy(ts->nodes[i]);

	free(nds);
	free(ts->nodes);
	free(ts);
	sp_werror(ename, ecode);
	free(ename);
	return -1;
}
