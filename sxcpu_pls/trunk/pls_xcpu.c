/* -*- C -*-
 *
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2006 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

/* @file:
 * xcpu Lancher to launch jobs on compute nodes..
 */

#include "orte_config.h"
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif  /* HAVE_SYS_TYPES_H */
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif  /* HAVE_SYS_STAT_H */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#include <errno.h>
#include <signal.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif  /* HAVE_FCNTL_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif  /* HAVE_STRING_H */

#include "opal/event/event.h"
#include "opal/mca/base/mca_base_param.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal/util/opal_environ.h"
#include "opal/util/path.h"
#include "opal/util/show_help.h"

#include "orte/dss/dss.h"
#include "orte/util/sys_info.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/gpr/base/base.h"
#include "orte/mca/iof/iof.h"
#include "orte/mca/ns/base/base.h"
#include "orte/mca/sds/base/base.h"
#include "orte/mca/oob/base/base.h"
#include "orte/mca/ras/base/base.h"
#include "orte/mca/rmgr/base/base.h"
#include "orte/mca/rmaps/base/base.h"
#include "orte/mca/rmaps/base/rmaps_base_map.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/smr/base/base.h"
#include "orte/runtime/orte_wait.h"
#include "orte/runtime/runtime.h"

#include "pls_xcpu.h"
#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxcpu.h"

extern char **environ;

/** external variable defined in libspclient */
extern int spc_chatty;

/**
 * Initialization of the xcpu module with all the needed function pointers
 */
orte_pls_base_module_t orte_pls_xcpu_module = {
	orte_pls_xcpu_launch,
	orte_pls_xcpu_terminate_job,
	orte_pls_xcpu_terminate_proc,
	orte_pls_xcpu_signal_job,
	orte_pls_xcpu_signal_proc,
	orte_pls_xcpu_finalize
};

Xpcommand *xcmd;
Xpnodeset *nds;

void
pls_xcpu_stdout_cb(Xpsession *s, u8 *buf, u32 buflen)
{
        fprintf(stdout, "%.*s", buflen, buf);
}

void
pls_xcpu_stderr_cb(Xpsession *s, u8 *buf, u32 buflen)
{
        fprintf(stderr, "%.*s", buflen, buf);
}

void
pls_xcpu_wait_cb(Xpsession *s, u8 *buf, u32 buflen)
{
        Xpnode *nd;

        nd = xp_session_get_node(s);

	/* FixMe: find out the process associated with this session */
	orte_smr.set_proc_state(nd->data, ORTE_PROC_STATE_TERMINATED, 0);
}


static char *
process_list(char **list, char sep)
{
	int i, n, len;
	char *s, *ret;
	char **items;

	/* find list length */
	for(n = 0; list[n] != NULL; n++)
		;

	items = calloc(n, sizeof(char *));
	if (!items)
		return NULL;

	/* quote the items if necessary */
	for(len = 0, i = 0; i < n; i++) {
		items[i] = quotestrdup(list[i]);
		len += strlen(items[i]) + 1;
	}

	ret = malloc(len+1);
	if (!ret)
		return NULL;

	for(s = ret, i = 0; i < n; i++) {
		len = strlen(items[i]);
		memcpy(s, items[i], len);
		s += len;
		*(s++) = sep;
		free(items[i]);
	}

	*s = '\0';
	free(items);
	return ret;
}

static char *
process_env(char **env)
{
	return process_list(env, '\n');
}

static char *
process_argv(char **argv)
{
	return process_list(argv, ' ');
}

static int
setup_env(char ***e)
{
	int n, rc;
	char *var, *param, *uri;
	char **env;


	n = opal_argv_count(*e);
	rc = mca_base_param_build_env(*e, &n, false);
	if (rc != ORTE_SUCCESS) {
		ORTE_ERROR_LOG(rc);
		return rc;
	}

	if (NULL != orte_process_info.ns_replica_uri) {
		uri = strdup(orte_process_info.ns_replica_uri);
	} else {
		uri = orte_rml.get_uri();
	}
	param = mca_base_param_environ_variable("ns", "replica", "uri");
	opal_setenv(param, uri, true, e);
	free(param);
	free(uri);

	if (NULL != orte_process_info.gpr_replica_uri) {
		uri = strdup(orte_process_info.gpr_replica_uri);
	} else {
		uri = orte_rml.get_uri();
	}
	param = mca_base_param_environ_variable("gpr", "replica", "uri");
	opal_setenv(param, uri, true, e);
	free(param);
	free(uri);

#if 0
	/* FixMe: Is this the frontend or backend nodename ? we don't have the starting
	 * daemon. */
	var = mca_base_param_environ_variable("orte", "base", "nodename");
	opal_setenv(var, orte_system_info.nodename, true, e);
	free(var);
#endif

	var = mca_base_param_environ_variable("universe", NULL, NULL);
	asprintf(&param, "%s@%s:%s", orte_universe_info.uid, 
		orte_universe_info.host, orte_universe_info.name);
	opal_setenv(var, param, true, e);

	free(param);
	free(var);

	/* FixMe: do this only when we oversubscribe */
        var = mca_base_param_environ_variable("mpi", NULL, "yield_when_idle");
        opal_setenv(var, "1", true, e);
	free(var);

#if 1
	/* merge in environment */
	env = opal_environ_merge(*e, environ);
	opal_argv_free(*e);
	*e = env;

	/* make sure hostname doesn't get pushed to backend node */
	//opal_unsetenv("HOSTNAME", e);
#endif

	return ORTE_SUCCESS;
}

/* This is the main function that will launch jobs on remote compute modes
 * @param jobid the jobid of the job to launch
 * @retval ORTE_SUCCESS or error
 */
int
orte_pls_xcpu_launch(orte_jobid_t jobid)
{
	int i, n, rc;
	opal_list_t mapping;
	orte_cellid_t cellid;
	opal_list_item_t *item;
	orte_rmaps_base_map_t* map;
	orte_rmaps_base_node_t *node;
	orte_rmaps_base_proc_t *proc;
	orte_vpid_t vpid_start, vpid_range;
	char **env;

	if (mca_pls_xcpu_component.chatty)
		spc_chatty = 1;


	OBJ_CONSTRUCT(&mapping, opal_list_t);
	rc = orte_rmaps_base_get_map(jobid, &mapping);
	if(rc != ORTE_SUCCESS) {
		ORTE_ERROR_LOG(rc);
		return rc;
	}

	/** next, get the vpid_start and range info so we can pass it along */
	rc = orte_rmaps_base_get_vpid_range(jobid, &vpid_start, &vpid_range);
	if (rc != ORTE_SUCCESS) {
		ORTE_ERROR_LOG(rc);
		return rc;
	}

	/* get the cellid */
	rc = orte_ns_base_get_cellid(&cellid, orte_process_info.my_name);
	if(ORTE_SUCCESS != rc) {
	    ORTE_ERROR_LOG(rc);
	    return rc;
	}

	/* create xcpu nodeset for mappings, FixME: how to create two 'mappings'
	 * by orterun? */
	nds = xp_nodeset_create();
	for (item = opal_list_get_first(&mapping), n = 0;
	     item != opal_list_get_end(&mapping);
	     item = opal_list_get_next(item)) {

		map = (orte_rmaps_base_map_t *) item;
		rc = setup_env(&map->app->env);
		if (rc != ORTE_SUCCESS) {
			ORTE_ERROR_LOG(rc);
			return rc;
		}
		rc = orte_ns_nds_xcpu_put(cellid, jobid, vpid_start, map->num_procs,
					  &map->app->env);
		if (rc != ORTE_SUCCESS) {
			ORTE_ERROR_LOG(rc);
			return rc;
		}
		for (i = 0; i < map->num_procs; i++, n++) {
			char * node_name;
			Xpnode *nd;
			proc = (orte_rmaps_base_proc_t *) map->procs[i];
			node = proc->proc_node;
			node_name = node->node->node_name;

			nd = xp_node_create(node_name, node_name, NULL, NULL);
			nd->data = &proc->proc_name;
			xp_nodeset_add(nds, nd);
		}
	}

	/* create xp_command from xp_nodeset */
	xcmd = xp_command_create(nds);

	if (xcmd == NULL) {
		goto error;
	}

	/* initialize the copypath (exec path) from the application contex */

	/* setup argc, argv and evn in xcpu command */
	xcmd->env = process_env(map->app->env);
	xcmd->argv = process_argv(map->app->argv);
	xcmd->exec = strdup(map->app->argv[0]);
	xcmd->copypath = strdup(map->app->argv[0]);
	asprintf(&xcmd->jobid, "%d", jobid);

	/* setup io forwarding */
	xcmd->stdout_cb = pls_xcpu_stdout_cb;
	xcmd->stderr_cb = pls_xcpu_stderr_cb;
	xcmd->wait_cb   = pls_xcpu_wait_cb;

	/* call xp_command_exec(xcmd) */
	if (xp_command_exec(xcmd) < 0)
		goto error;

	/* entering event loop and waiting for termination of processes
	 * by calling xp_command_wait */
	if (xp_command_wait(xcmd) < 0)
		goto error;

	OBJ_DESTRUCT(&mapping);
	return ORTE_SUCCESS;

error:
	/* error handling and clean up, kill all the processes */
	xp_command_wipe(xcmd);

	/* we destroy all sessions */
	xp_command_destroy(cmd);
	xcmd = NULL;

	OBJ_DESTRUCT(&mapping);
	/* set ORTE error code?? */
	return ORTE_ERROR;
}

int orte_pls_xcpu_terminate_job(orte_jobid_t jobid)
{
	fprintf(stderr, __FILE__ " terminate_job\n");

	/* libxcpu does not export the 'wipe' interface,
	 * we have to send sigkill to the session set
	 * FixME: ask lucho if xp_command|session_wipe can be implemented */
	xp_command_kill(xcmd, 9);

	return ORTE_SUCCESS;
}

int orte_pls_xcpu_terminate_proc(const orte_process_name_t* proc_name)
{
	fprintf(stderr, __FILE__ " terminate_proc\n");

	/* libxcpu can not wipe individual process in an
	 * Xpcommand/Xpsessionset, only to the whole session set */

	return ORTE_SUCCESS;
}

int orte_pls_xcpu_signal_job(orte_jobid_t jobid, int32_t sig)
{
	fprintf(stderr, __FILE__ " signal_job\n");

	xp_command_kill(xcmd, sig);

	return ORTE_SUCCESS;
}
int orte_pls_xcpu_signal_proc(const orte_process_name_t* proc_name, int32_t sig)
{
	fprintf(stderr, __FILE__ " terminate_proc\n");

	/* libxcpu can not send signal to individual process in an
	 * Xpcommand/Xpsessionset, only to the whole session set */

	return ORTE_SUCCESS;
}

int orte_pls_xcpu_finalize(void)
{
	if (xcmd != NULL)
		xp_command_destroy(xcmd);

	return ORTE_SUCCESS;
}

