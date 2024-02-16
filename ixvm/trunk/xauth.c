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
#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "xcpufs.h"

typedef struct Xauth Xauth;
struct Xauth {
	int	authdone;
	char	challenge[20];
	char	response[80];
};

static void
xauth_check_response(Xauth *a)
{
	// TODO
}

int
xauth_startauth(Spfid *afid, char *aname, Spqid *aqid)
{
	sp_werror("authentication not required", EIO);
	return 0;

/*
	Xauth *a;

	a = sp_malloc(sizeof(*a));
	if (!a)
		return NULL;

	a->authdone = 0;
	snprintf(a->challenge, sizeof(a->challenge), "%04x%04x", (unsigned int) random(), (unsigned int) random());
	memset(a->response, 0, sizeof(a->response));
	afid->aux = a;

	aqid->type = Qtauth;
	aqid.version = 0;
	aqid.path = 0;
	return 1;
*/
}

int
xauth_checkauth(Spfid *fid, Spfid *afid, char *aname)
{
	Xauth *a;

	a = afid->aux;

	if (fid->user != afid->user)
		goto error;

	if (a->authdone)
		return 1;

error:
	sp_werror("authentication failed", EIO);
	return 0;
}

int
xauth_read(Spfid *fid, u64 offset, u32 count, u8 *data)
{
	int n;
	Xauth *a;

	a = fid->aux;
	n = cutstr(data, offset, count, a->challenge, 0);
	return n;
}

int
xauth_write(Spfid *fid, u64 offset, u32 count, u8 *data)
{
	Xauth *a;

	a = fid->aux;
	if (offset+count > sizeof(a->response))
		count = sizeof(a->response) - offset;

	if (count <= 0)
		return 0;

	memmove(a->response + offset, data, count);
	if (offset + count == sizeof(a->response))
		xauth_check_response(a);

	return count;
}


int
xauth_clunk(Spfid *fid)
{
	free(fid->aux);
	fid->aux = NULL;
	return 1;
}

