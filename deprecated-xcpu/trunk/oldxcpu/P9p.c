#include "xcpusrv.h"
#include <libString.h>
#include <9pclient.h>

void
runit(void *client) {
	Client *c = (Client *)client;
	char **argv;
	int ntoken, i, kidfds[3];

	argv = emalloc9p(128 * sizeof(char *));

	if(c->argv == nil)
		c->argv = c->tmpname;
	ntoken = tokenize(c->argv, argv, 127);
	argv[ntoken] = 0;
	debug(12, "ntoken %d\n", ntoken);
	for(i = 0; i < ntoken; i++)
		debug(12, argv[i]);

	kidfds[0] = c->stdinfd[0];
	kidfds[1] = c->stdoutfd[1];
	kidfds[2] = c->stderrfd[1];

//	fprint(2, "kd %d kd %d kd %d\n", kidfds[0], kidfds[1], kidfds[2]);
	debug(12, "runit: spawn %s already\n", c->tmpname);

	switch(rfork(RFFDG|RFNOTEG|RFPROC)) {
	case -1:
		sysfatal("runit: fork: dead");
	case 0:
		dup2(kidfds[0], 0);
		dup2(kidfds[1], 1);
		dup2(kidfds[2], 2);
		close(kidfds[0]);
		close(kidfds[1]);
		close(kidfds[2]);
		/* and close all that other unused stuff */
		/*NOTE: THIS WAS NECESSARY. 
		  * It does not make much sense, but this made the difference 
		  * between hang or no hang to a /bin/sh when EOF was hit
		  * in 9rx
		  */
		close(c->stdinfd[1]);
		close(c->stdoutfd[0]);
		close(c->stderrfd[0]);
		execvp(c->tmpname, argv);
		sysfatal("runit: dead...\n");
	default:
		close(kidfds[0]);
		close(kidfds[1]);
		close(kidfds[2]);
		threadexits(0);
	}
}

void 
tmpexec(Client *c) 
{
		c->tmpname = smprint("/tmp/xcpuXXXXXX");
		/* create a file in /tmp */
		c->tmpfd = mkstemp(c->tmpname);
		if (c->tmpfd >= 0)
			fchmod(c->tmpfd, 0777); /* back to 700 later */
		else
			sysfatal("can not create temp file: %r");
}

extern int proclast;
extern char *procnames[];
extern int procstr[];

/* we accept a string (sexp?) of values we care for reporting */
void
procswrite(Req *r, Client *c)
{
	String *s;
	char *ptoks[proclast];
	char *chr;
	int i, j, toks, tmppstr[proclast];

	s = s_new();
	if(s == nil)
		respond(r, "can not allocate memory for data");

	/* BUG: we're assuming we fit a whole description in a single packet */
	s_nappend(s, r->ifcall.data, r->ifcall.count);

print("read: %s\n", s_to_c(s));
	/* flatten the sexp, it's not nested anyway, or shouldn't */
	if((chr = strchr(s_to_c(s), '(')) != nil)
		*chr = ' ';
	if((chr = strchr(s_to_c(s), ')')) != nil)
		*chr = ' ';

	
	toks = tokenize(s_to_c(s), ptoks, proclast);
	if(toks == 1 && strcmp(ptoks[0], "all") == 0) {
		for(i = 0; i < proclast; i++)
			tmppstr[i] = i;
	} else for(i = 0; i < toks; i++) {
		tmppstr[i] = -1;
		for(j = 0; j < proclast; j++) {
			if(strcmp(ptoks[i], procnames[j]) == 0)
				tmppstr[i] = j;
		}
		if(tmppstr[i] == -1) {
			s_free(s);
			s = s_new();
			s_append(s, "invalid string descriptor: ");
			s_append(s, ptoks[i]);
			s_append(s, "; transaction cancelled");
			respond(r, s_to_c(s));
			s_free(s);
			return;
		}
	}
	if(i < proclast)
		tmppstr[i] = -1;	/* terminate the list */

	for(i = 0; i < proclast; i++)
		procstr[i] = tmppstr[i]; 

	s_free(s);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}
