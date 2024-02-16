#include "xget.h"

int
init_server(int port)
{
        if( !(srv = sp_socksrv_create_tcp(&port)))
		return -1;
	spfile_init_srv(srv,root);
	srv->connopen = connopen;
	srv->connclose = connclose;
	srv->debuglevel = debuglevel & Dbgfs;
	srv->dotu = 0;
	srv->upool = upool;
	srv->flush = xflush;
	sp_srv_start(srv);
	numconnects = 0;
	debug(Dbgfn, "listen on %d\n", port);
	return 0;
}

void
connopen(Spconn *conn)
{
	numconnects++;
	debug(Dbgclnt, "Client %s connects %d\n", conn->address, numconnects);
}

void
connclose(Spconn *conn)
{
	numconnects--;
	debug(Dbgclnt, "Client %s disconnects %d\n", conn->address, numconnects);
}

Spfcall*
xflush(Spreq *req) 
{
	/* respond to reqs in dlreqs */

	return sp_create_rflush();
}

void
debug(int level, char *fmt, ...)
{
	va_list arg;
	char buf[512];

	if (!(debuglevel & level))
		return;
	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);

	fprintf(stderr, "%s", buf);

	if (logfid)
		spc_write(logfid, (u8 *) buf, strlen(buf), 0);
}

int
init_xget_usr() {
	/* Initialize 9P user pool */
	upool = sp_priv_userpool_create();
	if (!upool)
		return -1;

	debuglevel = 0;

	/* Initialize 9P user/group */
	user = sp_priv_user_add(upool, "root", 0, NULL);
	if (!user)
		return -1;

	group = sp_priv_group_add(upool, "root", 0);
	if (!group)
		return -1;

	sp_priv_user_setdfltgroup(user, group);
	return 0;
}
