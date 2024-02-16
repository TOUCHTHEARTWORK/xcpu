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
#include <sys/param.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <math.h>
#include <sys/types.h>
#include <pwd.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"

int
defaultuser(Spuser **puser, Spgroup **pgroup)
{
	static Spuserpool *upool = NULL;
	static struct passwd *xcpu_admin = NULL;
	static struct Spuser *adminuser = NULL;
	static struct Spgroup *admingroup = NULL;
	static Xkey *adminkey = NULL;
	gid_t xcpu_gid;

	if (puser)
		*puser = NULL;
	if (pgroup)
		*pgroup = NULL;

	if (! upool) {
		upool = sp_priv_userpool_create();
	}

	if (!upool) {
		fprintf(stderr, "can not create user pool\n");
		exit(1);
	}

	if (! adminkey)
		adminkey = xauth_privkey_create( "/etc/xcpu/admin_key");
	if (!adminkey) {
		adminkey = (Xkey *) ~0;
		sp_werror(NULL, 0);
	}

	if (! xcpu_admin)
		xcpu_admin = getpwnam("xcpu-admin");
	if (xcpu_admin)
		xcpu_gid = xcpu_admin->pw_gid;
	else
		xcpu_gid = 65530;
	if (! adminuser)
		adminuser = sp_priv_user_add(upool, "xcpu-admin", xcpu_gid, adminkey);
	if (!adminuser)
		goto error;

	if (! admingroup)
		admingroup = sp_priv_group_add(upool, "xcpu-admin", -3);
	if (!admingroup)
		goto error;

	if (puser)	
		*puser = adminuser;
	if (pgroup)
		*pgroup = admingroup;
	return 0;
error:
	return -1;

}

