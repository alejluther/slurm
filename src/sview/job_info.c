/*****************************************************************************\
 *  job_info.c - Functions related to job display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/common/uid.h"
#include "src/common/node_select.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"
#include <grp.h>

#define _DEBUG 0
#define MAX_CANCEL_RETRY 10
#define SIZE(a) (sizeof(a)/sizeof(a[0]))

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	int color_inx;
	job_info_t *job_ptr;
	int node_cnt;
	char *nodes;
	bool small_block;
	List step_list;
} sview_job_info_t;

static List foreach_list = NULL;
static char *stacked_job_list = NULL;

typedef struct {
	int job_id;
	int state;
	int step_id;
} jobs_foreach_t;

typedef struct {
	int edit_type;
	GtkWidget *entry;
	job_desc_msg_t *job_msg;
	char *type;
} jobs_foreach_common_t;

enum {
	EDIT_SIGNAL = 1,
	EDIT_SIGNAL_USER,
	EDIT_CANCEL,
	EDIT_CANCEL_USER,
	EDIT_REQUEUE,
	EDIT_SUSPEND,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNT,
	SORTID_ACTION,
	SORTID_ALLOC,
	SORTID_ALLOC_NODE,
	SORTID_ALPS_RESV_ID,
	SORTID_BATCH,
#ifdef HAVE_BG
	SORTID_NODELIST,
	SORTID_NODELIST_EXC,
	SORTID_NODELIST_REQ,
#endif
	SORTID_BLOCK,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_COMMAND,
	SORTID_COMMENT,
	SORTID_CONNECTION,
	SORTID_CONTIGUOUS,
/* 	SORTID_CORES_MAX, */
/* 	SORTID_CORES_MIN, */
	SORTID_CPUS,
	SORTID_CPU_MAX,
	SORTID_CPU_MIN,
	SORTID_CPUS_PER_TASK,
	SORTID_DEPENDENCY,
	SORTID_EXIT_CODE,
	SORTID_FEATURES,
	SORTID_GEOMETRY,
	SORTID_GRES,
	SORTID_GROUP_ID,
	SORTID_IMAGE_BLRTS,
#ifdef HAVE_BGL
	SORTID_IMAGE_LINUX,
	SORTID_IMAGE_MLOADER,
	SORTID_IMAGE_RAMDISK,
#else
	SORTID_IMAGE_LINUX,
	SORTID_IMAGE_RAMDISK,
	SORTID_IMAGE_MLOADER,
#endif
	SORTID_JOBID,
	SORTID_LICENSES,
	SORTID_CPU_REQ,
	SORTID_MEM_MIN,
	SORTID_TMP_DISK,
	SORTID_NAME,
	SORTID_NETWORK,
	SORTID_NICE,
#ifndef HAVE_BG
	SORTID_NODELIST,
	SORTID_NODELIST_EXC,
	SORTID_NODELIST_REQ,
#endif
	SORTID_NODE_INX,
	SORTID_NODES,
	SORTID_NODES_MAX,
	SORTID_NODES_MIN,
/* 	SORTID_NTASKS_PER_CORE, */
/* 	SORTID_NTASKS_PER_NODE, */
/* 	SORTID_NTASKS_PER_SOCKET, */
	SORTID_PARTITION,
	SORTID_PRIORITY,
	SORTID_QOS,
	SORTID_REASON,
	SORTID_REQUEUE,
	SORTID_RESV_NAME,
	SORTID_RESTARTS,
	SORTID_ROTATE,
	SORTID_SHARED,
/* 	SORTID_SOCKETS_MAX, */
/* 	SORTID_SOCKETS_MIN, */
	SORTID_STATE,
	SORTID_STATE_NUM,
	SORTID_TASKS,
/* 	SORTID_THREADS_MAX, */
/* 	SORTID_THREADS_MIN, */
	SORTID_TIME_ELIGIBLE,
	SORTID_TIME_END,
	SORTID_TIMELIMIT,
	SORTID_TIME_RESIZE,
	SORTID_TIME_RUNNING,
	SORTID_TIME_START,
	SORTID_TIME_SUBMIT,
	SORTID_TIME_SUSPEND,
	SORTID_SMALL_BLOCK,
	SORTID_UPDATED,
	SORTID_USER_ID,
	SORTID_WCKEY,
	SORTID_WORKDIR,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */

static display_data_t display_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_JOBID, "JobID", TRUE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_COLOR, NULL, TRUE, EDIT_COLOR,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ACTION, "Action", FALSE,
	 EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_ALLOC, NULL, FALSE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition", TRUE,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_BLOCK, "BG Block", TRUE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_GEOMETRY, "Geometry",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ROTATE, "Rotate",
	 FALSE, EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CONNECTION, "Connection",
	 FALSE, EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
#ifdef HAVE_BGL
	{G_TYPE_STRING, SORTID_IMAGE_BLRTS, "Image Blrts",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_LINUX, "Image Linux",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_RAMDISK, "Image Ramdisk",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_IMAGE_BLRTS, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_LINUX, "Image Cnload",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_RAMDISK, "Image Ioload",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_IMAGE_MLOADER, "Image Mloader",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_BLOCK, NULL, TRUE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_GEOMETRY, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ROTATE, NULL,
	 FALSE, EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CONNECTION, NULL,
	 FALSE, EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_BLRTS, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_LINUX, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_RAMDISK, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_IMAGE_MLOADER, NULL,
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#endif
#ifdef HAVE_CRAY
	{G_TYPE_STRING, SORTID_ALPS_RESV_ID, "ALPS Resv ID", TRUE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_ALPS_RESV_ID, NULL, TRUE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_USER_ID, "UserID", TRUE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_GROUP_ID, "GroupID", FALSE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_WCKEY, "WCKey", FALSE, EDIT_TEXTBOX, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NAME, "Name", TRUE, EDIT_TEXTBOX, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_STATE_NUM, NULL, FALSE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_RESIZE, "Time Resize", FALSE,
	 EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_RUNNING, "Time Running", TRUE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_SUBMIT, "Time Submit", FALSE,
	 EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_ELIGIBLE, "Time Eligible", FALSE,
	 EDIT_TEXTBOX, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_START, "Time Start", FALSE,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_END, "Time End", FALSE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIME_SUSPEND, "Time Suspended", FALSE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", FALSE,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES, "Node Count", TRUE, EDIT_TEXTBOX,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS, "CPU Count",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_EXC, "BP List Excluded",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_REQ, "BP List Requested",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", TRUE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_EXC, "NodeList Excluded",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODELIST_REQ, "NodeList Requested",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_CONTIGUOUS, "Contiguous", FALSE, EDIT_MODEL,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REQUEUE, "Requeue", FALSE, EDIT_MODEL,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_RESTARTS, "Restart Count", FALSE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_PRIORITY, "Priority", FALSE,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_EXIT_CODE, "Exit Code", FALSE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_BATCH, "Batch Flag", FALSE,
	 EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPU_MIN, "CPUs Min",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPU_MAX, "CPUs Max",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TASKS, "Task Count",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_SHARED, "Shared", FALSE,
	 EDIT_MODEL, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_CPUS_PER_TASK, "CPUs per Task",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_RESV_NAME, "Reservation Name",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES_MIN, "Nodes Min",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NODES_MAX, "Nodes Max",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
/* 	{G_TYPE_STRING, SORTID_SOCKETS_MIN, "Min Sockets",  */
/* 	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_SOCKETS_MAX, "Max Sockets",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_CORES_MIN, "Min Cores",  */
/* 	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_CORES_MAX, "Max Cores",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_THREADS_MIN, "Min Threads",  */
/* 	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_THREADS_MAX, "Max Threads",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
	{G_TYPE_STRING, SORTID_CPU_REQ, "Min CPUs Per Node",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_MEM_MIN, "Min Memory Per Node",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_TMP_DISK, "Min Tmp Disk Per Node",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_NICE, "Nice",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ACCOUNT, "Account",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_QOS, "QOS", FALSE,
	 EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_REASON, "Reason Waiting",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_FEATURES, "Features",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_GRES, "Gres",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_LICENSES, "Licenses",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_DEPENDENCY, "Dependency",
	 FALSE, EDIT_TEXTBOX, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_ALLOC_NODE, "Alloc Node : Sid",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
/* 	{G_TYPE_STRING, SORTID_NTASKS_PER_NODE, "Num tasks per Node",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_NTASKS_PER_SOCKET, "Num tasks per Socket",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
/* 	{G_TYPE_STRING, SORTID_NTASKS_PER_CORE, "Num tasks per Core",  */
/* 	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job}, */
#ifdef HAVE_AIX
	{G_TYPE_STRING, SORTID_NETWORK, "Network",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
#else
	{G_TYPE_STRING, SORTID_NETWORK, NULL,
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
#endif
	{G_TYPE_STRING, SORTID_COMMAND, "Command",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_COMMENT, "Comment",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_STRING, SORTID_WORKDIR, "Work Dir",
	 FALSE, EDIT_NONE, refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, FALSE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_POINTER, SORTID_NODE_INX, NULL, FALSE, EDIT_NONE,
	 refresh_job, create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_SMALL_BLOCK, NULL, FALSE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_job,
	 create_model_job, admin_edit_job},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_job[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, JOB_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Signal", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Requeue", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Cancel", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Suspend/Resume", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Edit Job", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partition", TRUE, JOB_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Block", TRUE, JOB_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, JOB_PAGE},
#else
	{G_TYPE_STRING, BLOCK_PAGE, NULL, TRUE, JOB_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, JOB_PAGE},
#endif
	{G_TYPE_STRING, RESV_PAGE, "Reservation", TRUE, JOB_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

struct signv {
	char *name;
	uint16_t val;
} sig_name_num[ ] = {
	{ "HUP",	SIGHUP  },
	{ "INT",	SIGINT  },
	{ "QUIT",	SIGQUIT },
	{ "ABRT",	SIGABRT },
	{ "KILL",	SIGKILL },
	{ "ALRM",	SIGALRM },
	{ "TERM",	SIGTERM },
	{ "USR1",	SIGUSR1 },
	{ "USR2",	SIGUSR2 },
	{ "CONT",	SIGCONT },
	{ "STOP",	SIGSTOP },
	{ "TSTP",	SIGTSTP },
	{ "TTIN",	SIGTTIN },
	{ "TTOU",	SIGTTOU },
	{ "SIGHUP",	SIGHUP  },
	{ "SIGINT",	SIGINT  },
	{ "SIGQUIT",	SIGQUIT },
	{ "SIGABRT",	SIGABRT },
	{ "SIGKILL",	SIGKILL },
	{ "SIGALRM",	SIGALRM },
	{ "SIGTERM",	SIGTERM },
	{ "SIGUSR1",	SIGUSR1 },
	{ "SIGUSR2",	SIGUSR2 },
	{ "SIGCONT",	SIGCONT },
	{ "SIGSTOP",	SIGSTOP },
	{ "SIGTSTP",	SIGTSTP },
	{ "SIGTTIN",	SIGTTIN },
	{ "SIGTTOU",	SIGTTOU }
};

static display_data_t *local_display_data = NULL;

static char *got_edit_signal = NULL;

static void _update_info_step(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter);


static void _job_info_list_del(void *object)
{
	sview_job_info_t *sview_job_info = (sview_job_info_t *)object;

	if (sview_job_info) {
		xfree(sview_job_info->nodes);
		if(sview_job_info->step_list)
			list_destroy(sview_job_info->step_list);
		xfree(sview_job_info);
	}
}

static void _destroy_jobs_foreach(void *object)
{
	jobs_foreach_t *jobs_foreach = (jobs_foreach_t *)object;

	if (jobs_foreach) {
		xfree(jobs_foreach);
	}
}

/* translate name name to number */
static uint16_t _xlate_signal_name(const char *signal_name)
{
	uint16_t sig_num = (uint16_t)NO_VAL;
	char *end_ptr, *sig_names = NULL;
	int i;

	sig_num = (uint16_t) strtol(signal_name, &end_ptr, 10);

	if ((*end_ptr == '\0') && (sig_num != 0))
		return sig_num;

	for (i=0; i<SIZE(sig_name_num); i++) {
		if (strcasecmp(sig_name_num[i].name, signal_name) == 0) {
			xfree(sig_names);
			return sig_name_num[i].val;
		}
		if (i == 0)
			sig_names = xstrdup(sig_name_num[i].name);
		else {
			xstrcat(sig_names, ",");
			xstrcat(sig_names, sig_name_num[i].name);
		}
	}
	xfree(sig_names);
	return (uint16_t)NO_VAL;
}

static int _cancel_job_id (uint32_t job_id, uint16_t signal)
{
	int error_code = SLURM_SUCCESS, i;
	char *temp = NULL;

	for (i=0; i<MAX_CANCEL_RETRY; i++) {
		if ((signal == (uint16_t)-1) || (signal == SIGKILL)) {
			signal = 9;
			error_code = slurm_kill_job(job_id, SIGKILL,
						    false);
		} else
			error_code = slurm_signal_job(job_id, signal);

		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		temp = g_strdup_printf("Sending signal %u to job %u",
				       signal, job_id);
		display_edit_note(temp);
		g_free(temp);
		sleep ( 5 + i );
	}
	if (error_code) {
		error_code = slurm_get_errno();
		if ((error_code != ESLURM_ALREADY_DONE) &&
		    (error_code != ESLURM_INVALID_JOB_ID)) {
			temp = g_strdup_printf(
				"Kill job error on job id %u: %s",
				job_id, slurm_strerror(slurm_get_errno()));
			display_edit_note(temp);
			g_free(temp);
		} else {
			display_edit_note(slurm_strerror(slurm_get_errno()));
		}
	}

	return error_code;
}

static int _cancel_step_id(uint32_t job_id, uint32_t step_id,
			   uint16_t signal)
{
	int error_code = SLURM_SUCCESS, i;
	char *temp = NULL;

	for (i=0; i<MAX_CANCEL_RETRY; i++) {
		if (signal == (uint16_t)-1 || (signal == SIGKILL)) {
			signal = 9;
			error_code = slurm_terminate_job_step(job_id, step_id);
		} else {
			error_code = slurm_signal_job_step(job_id, step_id,
							   signal);
		}
		if (error_code == 0
		    || (errno != ESLURM_TRANSITION_STATE_NO_UPDATE
			&& errno != ESLURM_JOB_PENDING))
			break;
		temp = g_strdup_printf("Sending signal %u to job step %u.%u",
				       signal, job_id, step_id);
		display_edit_note(temp);
		g_free(temp);
		sleep ( 5 + i );
	}

	if (error_code) {
		error_code = slurm_get_errno();
		if (error_code != ESLURM_ALREADY_DONE) {
			temp = g_strdup_printf(
				"Kill job error on job step id %u.%u: %s",
		 		job_id, step_id,
				slurm_strerror(slurm_get_errno()));
			display_edit_note(temp);
			g_free(temp);
		} else {
			display_edit_note(slurm_strerror(slurm_get_errno()));
		}
	}

	return error_code;
}

static void _set_active_combo_job(GtkComboBox *combo,
				  GtkTreeModel *model, GtkTreeIter *iter,
				  int type)
{
	char *temp_char = NULL;
	int action = 0;

	gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if(!temp_char)
		goto end_it;
	switch(type) {
	case SORTID_ACTION:
		if(!strcasecmp(temp_char, "None"))
			action = 0;
		else if(!strcasecmp(temp_char, "Cancel"))
			action = 1;
		else if(!strcasecmp(temp_char, "Suspend"))
			action = 2;
		else if(!strcasecmp(temp_char, "Resume"))
			action = 3;
		else if(!strcasecmp(temp_char, "Checkpoint"))
			action = 4;
		else if(!strcasecmp(temp_char, "Requeue"))
			action = 5;
		else
			action = 0;

		break;
	case SORTID_SHARED:
	case SORTID_CONTIGUOUS:
	case SORTID_REQUEUE:
	case SORTID_ROTATE:
		if(!strcasecmp(temp_char, "yes"))
			action = 0;
		else if(!strcasecmp(temp_char, "no"))
			action = 1;
		else
			action = 0;

		break;
	case SORTID_CONNECTION:
		if(!strcasecmp(temp_char, "Torus"))
			action = 0;
		else if(!strcasecmp(temp_char, "Mesh"))
			action = 1;
		else if(!strcasecmp(temp_char, "NAV"))
			action = 2;
		else if(!strcasecmp(temp_char, "HTC_S"))
			action = 3;
		else if(!strcasecmp(temp_char, "HTC_D"))
			action = 4;
		else if(!strcasecmp(temp_char, "HTC_V"))
			action = 5;
		else if(!strcasecmp(temp_char, "HTC_L"))
			action = 6;
		else
			action = 0;
		break;
	default:
		break;
	}
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);

}


/* don't free this char */
static const char *_set_job_msg(job_desc_msg_t *job_msg, const char *new_text,
				int column)
{
	char *type = "";
	int temp_int = 0;
	char *p;
	uint16_t rotate;
	uint16_t conn_type;
	char* token, *delimiter = ",x", *next_ptr;
	int j;
	uint16_t geo[SYSTEM_DIMENSIONS];
	char* geometry_tmp = xstrdup(new_text);
	char* original_ptr = geometry_tmp;

	/* need to clear global_edit_error here (just in case) */
	global_edit_error = 0;
	if(!job_msg)
		return NULL;

	switch(column) {
	case SORTID_ACTION:
		xfree(got_edit_signal);
		if(!strcasecmp(new_text, "None"))
			got_edit_signal = NULL;
		else
			got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_TIMELIMIT:
		if(!strcasecmp(new_text, "infinite"))
			temp_int = INFINITE;
		else
			temp_int = time_str2mins((char *)new_text);

		type = "timelimit";
		if(temp_int <= 0 && temp_int != INFINITE)
			goto return_error;
		job_msg->time_limit = (uint32_t)temp_int;
		break;
	case SORTID_PRIORITY:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "priority";
		if(temp_int < 0)
			goto return_error;
		job_msg->priority = (uint32_t)temp_int;
		break;
	case SORTID_NICE:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "nice";
		if (abs(temp_int) > NICE_OFFSET) {
			//error("Invalid nice value, must be between "
			//      "-%d and %d", NICE_OFFSET, NICE_OFFSET);
			goto return_error;
		}
		job_msg->nice = NICE_OFFSET + temp_int;
		break;
	case SORTID_CPU_REQ:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min cpus per node";
		if(temp_int <= 0)
			goto return_error;
		job_msg->pn_min_cpus = (uint32_t)temp_int;
		break;
	case SORTID_TASKS:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "requested tasks";
		if(temp_int <= 0)
			goto return_error;
		job_msg->num_tasks = (uint32_t)temp_int;
		break;
	case SORTID_CPUS_PER_TASK:
		temp_int = strtol(new_text, (char **)NULL, 10);

		type = "cpus per task";
		if(temp_int <= 0)
			goto return_error;
		job_msg->cpus_per_task = (uint32_t)temp_int;
		break;
	case SORTID_RESV_NAME:
		job_msg->reservation = xstrdup(new_text);
		type = "reservation name";
		break;
	case SORTID_NODES_MIN:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min nodes";
		if(temp_int <= 0)
			goto return_error;
		job_msg->min_nodes = (uint32_t)temp_int;
		break;
	case SORTID_NODES:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "node count";
		if(temp_int <= 0)
			goto return_error;
		job_msg->min_nodes = job_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_NODES_MAX:
		temp_int = strtol(new_text, &p, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "max nodes";
		if(temp_int <= 0)
			goto return_error;
		if(cluster_flags & CLUSTER_FLAG_BG)
			/* this needs to be set up for correct functionality */
			if(job_msg->min_nodes == NO_VAL)
				job_msg->min_nodes = (uint32_t)temp_int;

		job_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_MEM_MIN:
		temp_int = strtol(new_text, (char **)NULL, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min memory per node";
		if(temp_int <= 0)
			goto return_error;
		job_msg->pn_min_memory = (uint32_t)temp_int;
		break;
	case SORTID_TMP_DISK:
		temp_int = strtol(new_text, (char **)NULL, 10);
		if (*p == 'k' || *p == 'K')
			temp_int *= 1024;
		else if(*p == 'm' || *p == 'M')
			temp_int *= 1048576;

		type = "min tmp disk per node";
		if(temp_int <= 0)
			goto return_error;
		job_msg->pn_min_tmp_disk = (uint32_t)temp_int;
		break;
	case SORTID_PARTITION:
		job_msg->partition = xstrdup(new_text);
		type = "partition";
		break;
	case SORTID_NAME:
		job_msg->name = xstrdup(new_text);
		type = "name";
		break;
	case SORTID_WCKEY:
		job_msg->wckey = xstrdup(new_text);
		type = "wckey";
		break;
	case SORTID_SHARED:
		if (!strcasecmp(new_text, "yes"))
			job_msg->shared = 1;
		else
			job_msg->shared = 0;

		type = "shared";
		break;
	case SORTID_CONTIGUOUS:
		if (!strcasecmp(new_text, "yes"))
			job_msg->contiguous = 1;
		else
			job_msg->contiguous = 0;

		type = "contiguous";
		break;
	case SORTID_REQUEUE:
		if (!strcasecmp(new_text, "yes"))
			job_msg->requeue = 1;
		else
			job_msg->requeue = 0;

		type = "requeue";
		break;
	case SORTID_NODELIST_REQ:
		job_msg->req_nodes = xstrdup(new_text);
		type = "requested nodelist";
		break;
	case SORTID_NODELIST_EXC:
		job_msg->exc_nodes = xstrdup(new_text);
		type = "excluded nodelist";
		break;
	case SORTID_FEATURES:
		job_msg->features = xstrdup(new_text);
		type = "features";
		break;
	case SORTID_GRES:
		job_msg->gres = xstrdup(new_text);
		type = "gres";
		break;
	case SORTID_LICENSES:
		job_msg->licenses = xstrdup(new_text);
		type = "licenses";
		break;
	case SORTID_ACCOUNT:
		job_msg->account = xstrdup(new_text);
		type = "account";
		break;
	case SORTID_QOS:
		job_msg->qos = xstrdup(new_text);
		type = "qos";
		break;
	case SORTID_DEPENDENCY:
		job_msg->dependency = xstrdup(new_text);
		type = "dependency";
		break;
	case SORTID_GEOMETRY:
		type = "geometry";
		token = strtok_r(geometry_tmp, delimiter, &next_ptr);
		for (j=0; j<SYSTEM_DIMENSIONS; j++)
			geo[j] = (uint16_t) NO_VAL;
		for (j=0; j<SYSTEM_DIMENSIONS; j++) {
			if (!token) {
				//error("insufficient dimensions in "
				//      "Geometry");
				goto return_error;
			}
			geo[j] = (uint16_t) atoi(token);
			if (geo[j] <= 0) {
				//error("invalid --geometry argument");
				goto return_error;
				break;
			}
			geometry_tmp = next_ptr;
			token = strtok_r(geometry_tmp, delimiter,
					 &next_ptr);
		}
		if (token) {
			//error("too many dimensions in Geometry");
			goto return_error;
		}

		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_GEOMETRY,
					    (void *) &geo);
		break;
	case SORTID_ROTATE:
		type = "rotate";
		if (!strcasecmp(new_text, "yes")) {
			rotate = 1;

		} else {
			rotate = 0;

		}
		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_ROTATE,
					    (void *) &rotate);
		break;
	case SORTID_CONNECTION:
		type = "connection";
		if (!strcasecmp(new_text, "torus")) {
			conn_type = SELECT_TORUS;
		} else if (!strcasecmp(new_text, "mesh")) {
			conn_type = SELECT_MESH;
		} else if (!strcasecmp(new_text, "htc smp")) {
			conn_type = SELECT_HTC_S;
		} else if (!strcasecmp(new_text, "htc dual")) {
			conn_type = SELECT_HTC_D;
		} else if (!strcasecmp(new_text, "htc virtual")) {
			conn_type = SELECT_HTC_V;
		} else if (!strcasecmp(new_text, "htc linux")) {
			conn_type = SELECT_HTC_L;
		} else {
			conn_type = SELECT_NAV;
		}

		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_CONN_TYPE,
					    (void *) &conn_type);
		break;
	case SORTID_IMAGE_BLRTS:
		type = "BlrtsImage";
		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_BLRTS_IMAGE,
					    (void *) new_text);
		break;
	case SORTID_IMAGE_LINUX:
		if(cluster_flags & CLUSTER_FLAG_BGL)
			type = "LinuxImage";
		else
			type = "CnloadImage";

		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_LINUX_IMAGE,
					    (void *) new_text);
		break;
	case SORTID_IMAGE_MLOADER:
		type = "MloaderImage";
		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_MLOADER_IMAGE,
					    (void *) new_text);
		break;
	case SORTID_IMAGE_RAMDISK:
		if(cluster_flags & CLUSTER_FLAG_BGL)
			type = "RamdiskImage";
		else
			type = "IoloadImage";

		if(!job_msg->select_jobinfo)
			job_msg->select_jobinfo
				= select_g_select_jobinfo_alloc();
		select_g_select_jobinfo_set(job_msg->select_jobinfo,
					    SELECT_JOBDATA_RAMDISK_IMAGE,
					    (void *) new_text);
		break;
	case SORTID_TIME_ELIGIBLE:
	case SORTID_TIME_START:
		type = "start time";
		job_msg->begin_time = parse_time((char *)new_text, 0);
		if(!job_msg->begin_time)
			goto return_error;

		if(job_msg->begin_time < time(NULL))
			job_msg->begin_time = time(NULL);
		break;
	default:
		type = "unknown";
		break;
	}

	if(strcmp(type, "unknown"))
		global_send_update_msg = 1;

	xfree(original_ptr);
	return type;

return_error:
	xfree(original_ptr);
	global_edit_error = 1;
	return type;
}

static void _admin_edit_combo_box_job(GtkComboBox *combo,
				      job_desc_msg_t *job_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;

	if(!job_msg)
		return;

	if(!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if(!model) {
		g_print("nothing selected\n");
		return;
	}

	gtk_tree_model_get(model, &iter, 0, &name, -1);
	gtk_tree_model_get(model, &iter, 1, &column, -1);

	_set_job_msg(job_msg, name, column);

	g_free(name);
}

static gboolean _admin_focus_out_job(GtkEntry *entry,
				     GdkEventFocus *event,
				     job_desc_msg_t *job_msg)
{
	if(global_entry_changed) {
		const char *col_name = NULL;
		int type = gtk_entry_get_max_length(entry);
		const char *name = gtk_entry_get_text(entry);
		type -= DEFAULT_ENTRY_LENGTH;

		col_name = _set_job_msg(job_msg, name, type);
		if(global_edit_error) {
			if(global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup_printf(
				"Job %d %s can't be set to %s",
				job_msg->job_id,
				col_name,
				name);
		}
		global_entry_changed = 0;
	}
	return false;
}

static GtkWidget *_admin_full_edit_job(job_desc_msg_t *job_msg,
				       GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	display_data_t *display_data = display_data_job;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, FALSE);

	for(i = 0; i < SORTID_CNT; i++) {
		while(display_data++) {
			if(display_data->id == -1)
				break;
			if(!display_data->name)
				continue;
			if(display_data->id != i)
				continue;
			display_admin_edit(
				table, job_msg, &row, model, iter,
				display_data,
				G_CALLBACK(_admin_edit_combo_box_job),
				G_CALLBACK(_admin_focus_out_job),
				_set_active_combo_job);
			break;
		}
		display_data = display_data_job;
	}
	gtk_table_resize(table, row, 2);

	return GTK_WIDGET(window);
}

static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

static int _get_node_cnt(job_info_t * job)
{
	int node_cnt = 0;

	if (IS_JOB_PENDING(job) || IS_JOB_COMPLETING(job)) {
		node_cnt = _nodes_in_list(job->req_nodes);
		node_cnt = MAX(node_cnt, job->num_nodes);
	} else
		node_cnt = _nodes_in_list(job->nodes);
	return node_cnt;
}

/* this needs to be freed by xfree() */
static void _convert_char_to_job_and_step(const char *data,
					  int *jobid, int *stepid)
{
	int i = 0;

	if(!data)
		return;
	*jobid = atoi(data);
	*stepid = NO_VAL;
	while(data[i]) {
		if(data[i] == '.') {
			i++;
			if(data[i])
				*stepid = atoi(&data[i]);
			break;
		}
		i++;
	}

	return;
}

static void _layout_job_record(GtkTreeView *treeview,
			       sview_job_info_t *sview_job_info_ptr,
			       int update)
{
	char *nodes = NULL, *reason = NULL, *uname = NULL;
	char tmp_char[50];
	char running_char[50];
	time_t now_time = time(NULL);
	int suspend_secs = 0;
	job_info_t *job_ptr = sview_job_info_ptr->job_ptr;
	struct group *group_info = NULL;
	uint16_t term_sig = 0;

	GtkTreeIter iter;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if(!treestore)
		return;

	if(!job_ptr->nodes || !strcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(running_char,"00:00:00");
		nodes = "waiting...";
	} else {
		if (IS_JOB_SUSPENDED(job_ptr))
			now_time = job_ptr->pre_sus_time;
		else {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    (job_ptr->end_time != 0))
				now_time = job_ptr->end_time;
			if (job_ptr->suspend_time) {
				now_time = (time_t)
					(difftime(now_time,
						  job_ptr->suspend_time)
					 + job_ptr->pre_sus_time);
			} else
				now_time = (time_t)difftime(
					now_time, job_ptr->start_time);
		}
		suspend_secs = (time(NULL) - job_ptr->start_time) - now_time;
		secs2time_str(now_time, running_char, sizeof(running_char));

		nodes = sview_job_info_ptr->nodes;
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ACCOUNT),
				   job_ptr->account);

	snprintf(tmp_char, sizeof(tmp_char), "%s:%u",
		 job_ptr->alloc_node, job_ptr->alloc_sid);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_ALLOC_NODE),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_CRAYXT)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_ALPS_RESV_ID),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_DATA));
	if(job_ptr->batch_flag)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_BATCH),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST),
					   nodes);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST_EXC),
					   job_ptr->exc_nodes);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST_REQ),
					   job_ptr->req_nodes);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_BLOCK),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_BG_ID));
	}
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_COMMAND),
				   job_ptr->command);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_COMMENT),
				   job_ptr->comment);
	if(cluster_flags & CLUSTER_FLAG_BG)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_CONNECTION),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_CONNECTION));
	if(job_ptr->contiguous)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CONTIGUOUS),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->num_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->max_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->max_cpus);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_MAX),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->num_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_MIN),
				   tmp_char);

	if(job_ptr->cpus_per_task > 0)
		sprintf(tmp_char, "%u", job_ptr->cpus_per_task);
	else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPUS_PER_TASK),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_DEPENDENCY),
				   job_ptr->dependency);

	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	snprintf(tmp_char, sizeof(tmp_char), "%u:%u",
		 WEXITSTATUS(job_ptr->exit_code), term_sig);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_EXIT_CODE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_FEATURES),
				   job_ptr->features);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_GRES),
				   job_ptr->gres);

	if(cluster_flags & CLUSTER_FLAG_BG)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_GEOMETRY),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_GEOMETRY));

	group_info = getgrgid((gid_t)job_ptr->group_id);
	if (group_info && group_info->gr_name[0])
		snprintf(tmp_char, sizeof(tmp_char), "%s", group_info->gr_name);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->group_id);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_GROUP_ID),
				   tmp_char);
	if(cluster_flags & CLUSTER_FLAG_BG) {
		if(cluster_flags & CLUSTER_FLAG_BGL)
			add_display_treestore_line(
				update, treestore, &iter,
				find_col_name(display_data_job,
					      SORTID_IMAGE_BLRTS),
				select_g_select_jobinfo_sprint(
					job_ptr->select_jobinfo,
					tmp_char,
					sizeof(tmp_char),
					SELECT_PRINT_BLRTS_IMAGE));

		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_IMAGE_LINUX),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_LINUX_IMAGE));

		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_IMAGE_MLOADER),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_MLOADER_IMAGE));

		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_IMAGE_RAMDISK),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_RAMDISK_IMAGE));
	}

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->job_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_JOBID),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_LICENSES),
				   job_ptr->licenses);

	convert_num_unit((float)job_ptr->pn_min_cpus,
			 tmp_char, sizeof(tmp_char), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_REQ),
				   tmp_char);

	if(job_ptr->pn_min_memory > 0)
		convert_num_unit((float)job_ptr->pn_min_memory,
				 tmp_char, sizeof(tmp_char), UNIT_MEGA);
	else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_MEM_MIN),
				   tmp_char);
	if(job_ptr->pn_min_tmp_disk > 0)
		convert_num_unit((float)job_ptr->pn_min_tmp_disk,
				 tmp_char, sizeof(tmp_char), UNIT_MEGA);
	else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TMP_DISK),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NAME),
				   job_ptr->name);

	if(cluster_flags & CLUSTER_FLAG_AIX)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NETWORK),
					   job_ptr->network);

	if(job_ptr->pn_min_memory > 0)
		sprintf(tmp_char, "%u", job_ptr->nice - NICE_OFFSET);
	else
		sprintf(tmp_char, " ");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NICE),
				   tmp_char);

	if(!(cluster_flags & CLUSTER_FLAG_BG)) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST),
					   nodes);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST_EXC),
					   job_ptr->exc_nodes);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODELIST_REQ),
					   job_ptr->req_nodes);
	}

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)sview_job_info_ptr->node_cnt,
				 tmp_char, sizeof(tmp_char), UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 sview_job_info_ptr->node_cnt);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)sview_job_info_ptr->node_cnt,
				 tmp_char, sizeof(tmp_char), UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->max_nodes);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES_MAX),
				   tmp_char);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)sview_job_info_ptr->node_cnt,
				 tmp_char, sizeof(tmp_char), UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u",
			 job_ptr->num_nodes);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODES_MIN),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PARTITION),
				   job_ptr->partition);

	sprintf(tmp_char, "%u", job_ptr->priority);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PRIORITY),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_QOS),
				   job_ptr->qos);

	if (job_ptr->state_desc)
		reason = job_ptr->state_desc;
	else
		reason = job_reason_string(job_ptr->state_reason);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_REASON), reason);

	if(job_ptr->requeue)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_REQUEUE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_RESV_NAME),
				   job_ptr->resv_name);

	snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->restart_cnt);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_RESTARTS),
				   tmp_char);
	if(cluster_flags & CLUSTER_FLAG_BG)
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_ROTATE),
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_ROTATE));

	if(job_ptr->shared)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_SHARED),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STATE),
				   job_state_string(job_ptr->job_state));

	slurm_make_time_str((time_t *)&job_ptr->eligible_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_ELIGIBLE),
				   tmp_char);
	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_char, "Unknown");
	else
		slurm_make_time_str((time_t *)&job_ptr->end_time, tmp_char,
				    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_END),
				   tmp_char);

	if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_char, "Partition Limit");
	else if(job_ptr->time_limit == INFINITE)
		sprintf(tmp_char, "Infinite");
	else
		secs2time_str((job_ptr->time_limit * 60),
			      tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIMELIMIT),
				   tmp_char);

	if (job_ptr->resize_time) {
		slurm_make_time_str((time_t *)&job_ptr->resize_time, tmp_char,
				    sizeof(tmp_char));
	} else
		sprintf(tmp_char, "N/A");
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RESIZE),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RUNNING),
				   running_char);

	slurm_make_time_str((time_t *)&job_ptr->start_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_START),
				   tmp_char);
	slurm_make_time_str((time_t *)&job_ptr->submit_time, tmp_char,
			    sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_SUBMIT),
				   tmp_char);
	secs2time_str(suspend_secs, tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_SUSPEND),
				   tmp_char);

	uname = uid_to_string((uid_t)job_ptr->user_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_USER_ID), uname);
	xfree(uname);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_WCKEY),
				   job_ptr->wckey);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_WORKDIR),
				   job_ptr->work_dir);
}

static void _update_job_record(sview_job_info_t *sview_job_info_ptr,
			       GtkTreeStore *treestore,
			       GtkTreeIter *iter)
{
	char *nodes = NULL, *reason = NULL, *uname = NULL;
	char tmp_char[50];
	time_t now_time = time(NULL);
	int suspend_secs = 0;
	GtkTreeIter step_iter;
	int childern = 0;
	job_info_t *job_ptr = sview_job_info_ptr->job_ptr;
	struct group *group_info = NULL;
	uint16_t term_sig = 0;

	if(cluster_flags & CLUSTER_FLAG_BG)
		gtk_tree_store_set(treestore, iter, SORTID_SMALL_BLOCK,
				   sview_job_info_ptr->small_block, -1);

	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);
	if(!job_ptr->nodes || !strcasecmp(job_ptr->nodes,"waiting...")) {
		sprintf(tmp_char,"00:00:00");
		nodes = "waiting...";
	} else {
		if (IS_JOB_SUSPENDED(job_ptr))
			now_time = job_ptr->pre_sus_time;
		else {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    (job_ptr->end_time != 0))
				now_time = job_ptr->end_time;
			if (job_ptr->suspend_time) {
				now_time = (time_t)
					(difftime(now_time,
						  job_ptr->suspend_time)
					 + job_ptr->pre_sus_time);
			} else
				now_time = (time_t)difftime(
					now_time, job_ptr->start_time);
		}
		suspend_secs = (time(NULL) - job_ptr->start_time) - now_time;
		secs2time_str(now_time, tmp_char, sizeof(tmp_char));
		nodes = sview_job_info_ptr->nodes;
	}
	gtk_tree_store_set(treestore, iter, SORTID_COLOR,
			   sview_colors[sview_job_info_ptr->color_inx], -1);
	gtk_tree_store_set(treestore, iter, SORTID_COLOR_INX,
			   sview_job_info_ptr->color_inx, -1);

	gtk_tree_store_set(treestore, iter, SORTID_TIME_RUNNING, tmp_char, -1);
	if (job_ptr->resize_time) {
		slurm_make_time_str((time_t *)&job_ptr->resize_time, tmp_char,
				    sizeof(tmp_char));
	} else
		sprintf(tmp_char, "N/A");
	gtk_tree_store_set(treestore, iter, SORTID_TIME_RESIZE, tmp_char, -1);
	slurm_make_time_str((time_t *)&job_ptr->submit_time, tmp_char,
			    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIME_SUBMIT, tmp_char, -1);
	slurm_make_time_str((time_t *)&job_ptr->eligible_time, tmp_char,
			    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIME_ELIGIBLE, tmp_char, -1);
	slurm_make_time_str((time_t *)&job_ptr->start_time, tmp_char,
			    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIME_START, tmp_char, -1);
	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_char, "Unknown");
	else
		slurm_make_time_str((time_t *)&job_ptr->end_time, tmp_char,
				    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIME_END, tmp_char, -1);
	secs2time_str(suspend_secs, tmp_char, sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIME_SUSPEND, tmp_char, -1);

	if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_char, "Partition Limit");
	else if(job_ptr->time_limit == INFINITE)
		sprintf(tmp_char, "Infinite");
	else
		secs2time_str((job_ptr->time_limit * 60),
			      tmp_char, sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_TIMELIMIT, tmp_char, -1);

	gtk_tree_store_set(treestore, iter, SORTID_ALLOC, 1, -1);
	gtk_tree_store_set(treestore, iter, SORTID_JOBID, job_ptr->job_id, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_PARTITION, job_ptr->partition, -1);
	snprintf(tmp_char, sizeof(tmp_char), "%s:%u",
		 job_ptr->alloc_node, job_ptr->alloc_sid);
	gtk_tree_store_set(treestore, iter, SORTID_ALLOC_NODE, tmp_char, -1);

	group_info = getgrgid((gid_t) job_ptr->group_id );
	if ( group_info && group_info->gr_name[ 0 ] )
		snprintf(tmp_char, sizeof(tmp_char), "%s", group_info->gr_name);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->group_id);

	gtk_tree_store_set(treestore, iter,
			   SORTID_GROUP_ID,
			   tmp_char, -1);

	uname = uid_to_string((uid_t)job_ptr->user_id);
	gtk_tree_store_set(treestore, iter,
			   SORTID_USER_ID, uname, -1);
	xfree(uname);

	if(cluster_flags & CLUSTER_FLAG_BG) {
		gtk_tree_store_set(treestore, iter,
				   SORTID_BLOCK,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_BG_ID), -1);

		gtk_tree_store_set(treestore, iter,
				   SORTID_CONNECTION,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_CONNECTION), -1);
		gtk_tree_store_set(treestore, iter,
				   SORTID_GEOMETRY,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_GEOMETRY), -1);
		gtk_tree_store_set(treestore, iter,
				   SORTID_ROTATE,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_ROTATE), -1);
		if(cluster_flags & CLUSTER_FLAG_BGL)
			gtk_tree_store_set(treestore, iter,
					   SORTID_IMAGE_BLRTS,
					   select_g_select_jobinfo_sprint(
						   job_ptr->select_jobinfo,
						   tmp_char,
						   sizeof(tmp_char),
						   SELECT_PRINT_BLRTS_IMAGE),
					   -1);
		gtk_tree_store_set(treestore, iter,
				   SORTID_IMAGE_LINUX,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_LINUX_IMAGE), -1);

		gtk_tree_store_set(treestore, iter,
				   SORTID_IMAGE_MLOADER,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_MLOADER_IMAGE), -1);

		gtk_tree_store_set(treestore, iter,
				   SORTID_IMAGE_RAMDISK,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_RAMDISK_IMAGE), -1);
	}

	if(cluster_flags & CLUSTER_FLAG_CRAYXT)
		gtk_tree_store_set(treestore, iter,
				   SORTID_ALPS_RESV_ID,
				   select_g_select_jobinfo_sprint(
					   job_ptr->select_jobinfo,
					   tmp_char,
					   sizeof(tmp_char),
					   SELECT_PRINT_DATA), -1);

	gtk_tree_store_set(treestore, iter, SORTID_NAME, job_ptr->name, -1);
	gtk_tree_store_set(treestore, iter, SORTID_WCKEY, job_ptr->wckey, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_STATE,
			   job_state_string(job_ptr->job_state), -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_STATE_NUM,
			   job_ptr->job_state, -1);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)sview_job_info_ptr->node_cnt,
				 tmp_char, sizeof(tmp_char), UNIT_NONE);
	else
		sprintf(tmp_char, "%u", sview_job_info_ptr->node_cnt);

	gtk_tree_store_set(treestore, iter,
			   SORTID_NODES, tmp_char, -1);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->num_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);

	gtk_tree_store_set(treestore, iter,
			   SORTID_CPUS, tmp_char, -1);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->num_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->num_cpus);

	gtk_tree_store_set(treestore, iter,
			   SORTID_CPU_MIN, tmp_char, -1);

	if(cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)job_ptr->max_cpus,
				 tmp_char, sizeof(tmp_char),
				 UNIT_NONE);
	else
		snprintf(tmp_char, sizeof(tmp_char), "%u", job_ptr->max_cpus);

	gtk_tree_store_set(treestore, iter,
			   SORTID_CPU_MAX, tmp_char, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, nodes, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_NODE_INX, job_ptr->node_inx, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NODELIST_REQ,
			   job_ptr->req_nodes, -1);
	gtk_tree_store_set(treestore, iter, SORTID_NODELIST_EXC,
			   job_ptr->exc_nodes, -1);

	if(job_ptr->contiguous)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");

	gtk_tree_store_set(treestore, iter,
			   SORTID_CONTIGUOUS, tmp_char, -1);
	if(job_ptr->shared)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	gtk_tree_store_set(treestore, iter,
			   SORTID_SHARED, tmp_char, -1);

	if(job_ptr->batch_flag)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	gtk_tree_store_set(treestore, iter,
			   SORTID_BATCH, tmp_char, -1);

	if(job_ptr->requeue)
		sprintf(tmp_char, "yes");
	else
		sprintf(tmp_char, "no");
	gtk_tree_store_set(treestore, iter,
			   SORTID_REQUEUE, tmp_char, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_RESTARTS, job_ptr->restart_cnt, -1);

	sprintf(tmp_char, "%u", sview_job_info_ptr->node_cnt);
	gtk_tree_store_set(treestore, iter,
			   SORTID_NODES_MIN, tmp_char, -1);
	if(job_ptr->max_nodes > 0) {
		sprintf(tmp_char, "%u", sview_job_info_ptr->node_cnt);
		gtk_tree_store_set(treestore, iter,
				   SORTID_NODES_MAX, tmp_char, -1);
	}
	if(job_ptr->cpus_per_task > 0) {
		sprintf(tmp_char, "%u", job_ptr->cpus_per_task);
		gtk_tree_store_set(treestore, iter,
				   SORTID_CPUS_PER_TASK, tmp_char, -1);
	}
	convert_num_unit((float)job_ptr->pn_min_cpus,
			 tmp_char, sizeof(tmp_char), UNIT_NONE);
	gtk_tree_store_set(treestore, iter,
			   SORTID_CPU_REQ, tmp_char, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_RESV_NAME, job_ptr->resv_name, -1);

/* 	sprintf(tmp_char, "%u", job_ptr->min_sockets); */
/* 	gtk_tree_store_set(treestore, iter, */
/* 			   SORTID_SOCKETS_MIN, tmp_char, -1); */

/* 	sprintf(tmp_char, "%u", job_ptr->min_cores); */
/* 	gtk_tree_store_set(treestore, iter, */
/* 			   SORTID_CORES_MIN, tmp_char, -1); */

/* 	sprintf(tmp_char, "%u", job_ptr->min_threads); */
/* 	gtk_tree_store_set(treestore, iter, */
/* 			   SORTID_THREADS_MIN, tmp_char, -1); */

	convert_num_unit((float)job_ptr->pn_min_memory,
			 tmp_char, sizeof(tmp_char), UNIT_MEGA);
	gtk_tree_store_set(treestore, iter,
			   SORTID_MEM_MIN, tmp_char, -1);

	convert_num_unit((float)job_ptr->pn_min_tmp_disk,
			 tmp_char, sizeof(tmp_char), UNIT_MEGA);
	gtk_tree_store_set(treestore, iter,
			   SORTID_TMP_DISK, tmp_char, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_ACCOUNT, job_ptr->account, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_QOS, job_ptr->qos, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_DEPENDENCY, job_ptr->dependency, -1);

	sprintf(tmp_char, "%u", job_ptr->priority);
	gtk_tree_store_set(treestore, iter,
			   SORTID_PRIORITY, tmp_char, -1);

	if(WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	snprintf(tmp_char, sizeof(tmp_char), "%u:%u",
		 WEXITSTATUS(job_ptr->exit_code), term_sig);
	gtk_tree_store_set(treestore, iter,
			   SORTID_EXIT_CODE, tmp_char, -1);


	gtk_tree_store_set(treestore, iter,
			   SORTID_FEATURES, job_ptr->features, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_GRES, job_ptr->gres, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_LICENSES, job_ptr->licenses, -1);
	if (job_ptr->state_desc)
		reason = job_ptr->state_desc;
	else
		reason = job_reason_string(job_ptr->state_reason);
	gtk_tree_store_set(treestore, iter,
			   SORTID_REASON, reason, -1);

	if(cluster_flags & CLUSTER_FLAG_AIX)
		gtk_tree_store_set(treestore, iter,
				   SORTID_NETWORK, job_ptr->network, -1);

	sprintf(tmp_char, "%u", job_ptr->nice - NICE_OFFSET);
	gtk_tree_store_set(treestore, iter,
			   SORTID_NICE, tmp_char, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_COMMAND, job_ptr->command, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_COMMENT, job_ptr->comment, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_WORKDIR, job_ptr->work_dir, -1);


	childern = gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						&step_iter, iter);
	if(gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
					&step_iter, iter))
		_update_info_step(sview_job_info_ptr,
				  GTK_TREE_MODEL(treestore), &step_iter, iter);
	else
		_update_info_step(sview_job_info_ptr,
				  GTK_TREE_MODEL(treestore), NULL, iter);

	return;
}

static void _layout_step_record(GtkTreeView *treeview,
				job_step_info_t *step_ptr,
				int update)
{
	char *nodes = NULL, *uname;
	char tmp_char[50];
	char tmp_time[50];
	time_t now_time = time(NULL);
	GtkTreeIter iter;
	enum job_states state;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	if(!treestore)
		return;

	convert_num_unit((float)step_ptr->num_cpus, tmp_char, sizeof(tmp_char),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_CPU_MIN),
				   tmp_char);

	uname = uid_to_string((uid_t)step_ptr->user_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_USER_ID), uname);
	xfree(uname);

	snprintf(tmp_char, sizeof(tmp_char), "%u.%u",
		 step_ptr->job_id,
		 step_ptr->step_id);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_JOBID),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_GRES),
				   step_ptr->gres);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NAME),
				   step_ptr->name);

	if(!step_ptr->nodes
	   || !strcasecmp(step_ptr->nodes,"waiting...")) {
		sprintf(tmp_time,"00:00:00");
		nodes = "waiting...";
		state = JOB_PENDING;
	} else {
		now_time -= step_ptr->start_time;
		secs2time_str(now_time, tmp_time, sizeof(tmp_time));
		nodes = step_ptr->nodes;
		if(cluster_flags & CLUSTER_FLAG_BG)
			convert_num_unit(
				(float)step_ptr->num_tasks / cpus_per_node,
				tmp_char, sizeof(tmp_char), UNIT_NONE);
		else
			convert_num_unit((float)_nodes_in_list(nodes),
					 tmp_char, sizeof(tmp_char), UNIT_NONE);

		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_job,
							 SORTID_NODES),
					   tmp_char);
		state = JOB_RUNNING;
	}

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_NODELIST),
				   nodes);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_PARTITION),
				   step_ptr->partition);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_STATE),
				   job_state_string(state));

	if (step_ptr->time_limit == NO_VAL)
		sprintf(tmp_char, "Partition Limit");
	else if(step_ptr->time_limit == INFINITE)
		sprintf(tmp_char, "Infinite");
	else
		secs2time_str((step_ptr->time_limit * 60),
			      tmp_char, sizeof(tmp_char));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIMELIMIT),
				   tmp_char);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TIME_RUNNING),
				   tmp_time);

	convert_num_unit((float)step_ptr->num_tasks, tmp_char, sizeof(tmp_char),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_job,
						 SORTID_TASKS),
				   tmp_char);
}

static void _update_step_record(job_step_info_t *step_ptr,
				GtkTreeStore *treestore,
				GtkTreeIter *iter)
{
	char *nodes = NULL, *uname = NULL;
	char tmp_char[50];
	char tmp_time[50];
	time_t now_time = time(NULL);
	enum job_states state;

	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);
	if(!step_ptr->nodes
	   || !strcasecmp(step_ptr->nodes,"waiting...")) {
		sprintf(tmp_time,"00:00:00");
		nodes = "waiting...";
		state = JOB_PENDING;
	} else {
		now_time -= step_ptr->start_time;
		secs2time_str(now_time, tmp_time, sizeof(tmp_time));
		nodes = step_ptr->nodes;
		if(cluster_flags & CLUSTER_FLAG_BG)
			convert_num_unit(
				(float)step_ptr->num_tasks / cpus_per_node,
				tmp_char, sizeof(tmp_char), UNIT_NONE);
		else
			convert_num_unit((float)_nodes_in_list(nodes),
					 tmp_char, sizeof(tmp_char), UNIT_NONE);

		gtk_tree_store_set(treestore, iter,
				   SORTID_NODES, tmp_char, -1);
		state = JOB_RUNNING;
	}

	gtk_tree_store_set(treestore, iter,
			   SORTID_STATE,
			   job_state_string(state), -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_TIME_RUNNING, tmp_time, -1);

	gtk_tree_store_set(treestore, iter, SORTID_ALLOC, 0, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_JOBID, step_ptr->step_id, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_PARTITION, step_ptr->partition, -1);
	gtk_tree_store_set(treestore, iter,
			   SORTID_GRES, step_ptr->gres, -1);
/* #ifdef HAVE_BG */
/* 	gtk_tree_store_set(treestore, iter,  */
/* 			   SORTID_BLOCK,  */
/* 			   select_g_select_jobinfo_sprint( */
/* 				   step_ptr->select_jobinfo,  */
/* 				   tmp_char,  */
/* 				   sizeof(tmp_char),  */
/* 				   SELECT_PRINT_BG_ID), -1); */
/* #endif */
	uname = uid_to_string((uid_t)step_ptr->user_id);
	gtk_tree_store_set(treestore, iter,
			   SORTID_USER_ID, uname, -1);
	xfree(uname);
	gtk_tree_store_set(treestore, iter,
			   SORTID_NAME, step_ptr->name, -1);

	convert_num_unit((float)step_ptr->num_tasks, tmp_char, sizeof(tmp_char),
			 UNIT_NONE);
	gtk_tree_store_set(treestore, iter,
			   SORTID_TASKS, tmp_char, -1);

	convert_num_unit((float)step_ptr->num_cpus, tmp_char, sizeof(tmp_char),
			 UNIT_NONE);
	gtk_tree_store_set(treestore, iter,
			   SORTID_CPU_MIN, tmp_char, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_NODELIST, nodes, -1);

	return;
}

static void _append_job_record(sview_job_info_t *sview_job_info_ptr,
			       GtkTreeStore *treestore, GtkTreeIter *iter,
			       int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_job_record(sview_job_info_ptr, treestore, iter);
}

static void _append_step_record(job_step_info_t *step_ptr,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int jobid)
{
	GtkTreeIter step_iter;

	gtk_tree_store_append(treestore, &step_iter, iter);
	gtk_tree_store_set(treestore, &step_iter, SORTID_POS, jobid, -1);
	_update_step_record(step_ptr, treestore, &step_iter);
}

static void _update_info_step(sview_job_info_t *sview_job_info_ptr,
			      GtkTreeModel *model,
			      GtkTreeIter *step_iter,
			      GtkTreeIter *iter)
{
	int stepid = 0;
	int i;
	GtkTreeIter first_step_iter;
	int set = 0;
	ListIterator itr = NULL;
	job_step_info_t *step_ptr = NULL;

	memset(&first_step_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (step_iter) {
		first_step_iter = *step_iter;
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), step_iter,
					   SORTID_UPDATED, 0, -1);
			if(!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
		memcpy(step_iter, &first_step_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	itr = list_iterator_create(sview_job_info_ptr->step_list);
	while((step_ptr = list_next(itr))) {
		/* get the iter, or find out the list is empty goto add */
		if (!step_iter) {
			goto adding;
		} else {
			memcpy(step_iter, &first_step_iter,
			       sizeof(GtkTreeIter));
		}
		while(1) {
			/* search for the jobid and check to see if
			   it is in the list */
			gtk_tree_model_get(model, step_iter, SORTID_JOBID,
					   &stepid, -1);
			if(stepid == (int)step_ptr->step_id) {
				/* update with new info */
				_update_step_record(step_ptr,
						    GTK_TREE_STORE(model),
						    step_iter);
				goto found;
			}

			if(!gtk_tree_model_iter_next(model, step_iter)) {
				step_iter = NULL;
				break;
			}
		}
	adding:
		_append_step_record(step_ptr, GTK_TREE_STORE(model),
				    iter, sview_job_info_ptr->job_ptr->job_id);
	found:
		;
	}
	list_iterator_destroy(itr);

	if(set) {
		step_iter = &first_step_iter;
		/* clear all steps that aren't active */
		while(1) {
			gtk_tree_model_get(model, step_iter,
					   SORTID_UPDATED, &i, -1);
			if(!i) {
				if(!gtk_tree_store_remove(
					   GTK_TREE_STORE(model),
					   step_iter))
					break;
				else
					continue;
			}
			if(!gtk_tree_model_iter_next(model, step_iter)) {
				break;
			}
		}
	}
	return;
}

static void _update_info_job(List info_list,
			     GtkTreeView *tree_view)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	int jobid = 0;
	job_info_t *job_ptr = NULL;
	int line = 0;
	char *host = NULL;
	ListIterator itr = NULL;
	sview_job_info_t *sview_job_info = NULL;

	/* make sure all the jobs are still here */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
					   SORTID_UPDATED, 0, -1);
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	itr = list_iterator_create(info_list);
	while ((sview_job_info = (sview_job_info_t*) list_next(itr))) {
		job_ptr = sview_job_info->job_ptr;
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		}
		line = 0;
		while(1) {
			/* search for the jobid and check to see if
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_JOBID,
					   &jobid, -1);
			if(jobid == job_ptr->job_id) {
				/* update with new info */
				_update_job_record(sview_job_info,
						   GTK_TREE_STORE(model),
						   &iter);
				goto found;
			}

			line++;
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	adding:
		_append_job_record(sview_job_info, GTK_TREE_STORE(model),
				   &iter, line);
	found:
		;
	}
	list_iterator_destroy(itr);
	if(host)
		free(host);
	gtk_tree_path_free(path);
	/* remove all old jobs */
	remove_old(model, SORTID_UPDATED);
	return;
}

static int _sview_job_sort_aval_dec(sview_job_info_t* rec_a,
				    sview_job_info_t* rec_b)
{
	int size_a = rec_a->node_cnt;
	int size_b = rec_b->node_cnt;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;

	if(rec_a->nodes && rec_b->nodes) {
		size_a = strcmp(rec_a->nodes, rec_b->nodes);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	return 0;
}

static List _create_job_info_list(job_info_msg_t *job_info_ptr,
				  job_step_info_response_msg_t *step_info_ptr,
				  int changed, int want_odd_states)
{
	static List info_list = NULL;
	static List odd_info_list = NULL;
	int i = 0, j = 0;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	job_step_info_t *step_ptr = NULL;
	char *ionodes = NULL;
	char tmp_char[50];

	if(!changed && info_list) {
		goto update_color;
	}

	if(info_list) {
		list_flush(info_list);
		list_flush(odd_info_list);
	} else {
		info_list = list_create(NULL);
		odd_info_list = list_create(_job_info_list_del);
	}
	if (!info_list || !odd_info_list) {
		g_print("malloc error\n");
		return NULL;
	}

	for(i=0; i<job_info_ptr->record_count; i++) {
		job_ptr = &(job_info_ptr->job_array[i]);
		if (strstr(working_sview_config.excluded_partitions, job_ptr->partition))
			continue;
		sview_job_info_ptr = xmalloc(sizeof(sview_job_info_t));
		sview_job_info_ptr->job_ptr = job_ptr;
		sview_job_info_ptr->step_list = list_create(NULL);
		sview_job_info_ptr->node_cnt = 0;
		sview_job_info_ptr->color_inx =
			job_ptr->job_id % sview_colors_cnt;
		if(cluster_flags & CLUSTER_FLAG_BG) {
			select_g_select_jobinfo_get(job_ptr->select_jobinfo,
						    SELECT_JOBDATA_IONODES,
						    &ionodes);
			select_g_select_jobinfo_get(
				job_ptr->select_jobinfo,
				SELECT_JOBDATA_NODE_CNT,
				&sview_job_info_ptr->node_cnt);
			if(job_ptr->nodes && ionodes) {
				sview_job_info_ptr->small_block = 1;
				snprintf(tmp_char, sizeof(tmp_char), "%s[%s]",
					 job_ptr->nodes, ionodes);
				xfree(ionodes);
				/* keep a different string here so we don't
				   just keep tacking on ionodes to a
				   node list */
				sview_job_info_ptr->nodes = xstrdup(tmp_char);
			} else
				sview_job_info_ptr->nodes =
					xstrdup(job_ptr->nodes);
		} else
			sview_job_info_ptr->nodes = xstrdup(job_ptr->nodes);

		if(!sview_job_info_ptr->node_cnt)
			sview_job_info_ptr->node_cnt = _get_node_cnt(job_ptr);

		for(j = 0; j < step_info_ptr->job_step_count; j++) {
			step_ptr = &(step_info_ptr->job_steps[j]);
			if(step_ptr->job_id == job_ptr->job_id) {
				list_append(sview_job_info_ptr->step_list,
					    step_ptr);
			}
		}
		list_append(odd_info_list, sview_job_info_ptr);

		if(!IS_JOB_PENDING(job_ptr) &&
		   !IS_JOB_RUNNING(job_ptr) &&
		   !IS_JOB_SUSPENDED(job_ptr) &&
		   !IS_JOB_COMPLETING(job_ptr)) {
			continue;
		}
		list_append(info_list, sview_job_info_ptr);
	}

	list_sort(info_list, (ListCmpF)_sview_job_sort_aval_dec);

	list_sort(odd_info_list, (ListCmpF)_sview_job_sort_aval_dec);

update_color:

	if(want_odd_states)
		return odd_info_list;
	else
		return info_list;

}

void _display_info_job(List info_list, popup_info_t *popup_win)
{
	job_step_info_t *step_ptr;
	specific_info_t *spec_info = popup_win->spec_info;
	ListIterator itr = NULL;
	sview_job_info_t *sview_job_info = NULL;
	int found = 0;
	GtkTreeView *treeview = NULL;
	int update = 0;
	int j = 0;

	if(spec_info->search_info->int_data == NO_VAL) {
		/* 	info = xstrdup("No pointer given!"); */
		goto finished;
	}

need_refresh:
	if(!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	itr = list_iterator_create(info_list);
	while((sview_job_info = (sview_job_info_t*) list_next(itr))) {
		if(sview_job_info->job_ptr->job_id ==
		   spec_info->search_info->int_data)
			break;
	}
	list_iterator_destroy(itr);

	if(!sview_job_info) {
		/* not found */
	} else if(spec_info->search_info->int_data2 == NO_VAL) {
		j=0;
		while(sview_job_info->job_ptr->node_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				sview_job_info->job_ptr->node_inx[j],
				sview_job_info->job_ptr->node_inx[j+1],
				sview_job_info->color_inx,
				true, 0);
			j += 2;
		}
		_layout_job_record(treeview, sview_job_info, update);
		found = 1;
	} else {
		itr = list_iterator_create(sview_job_info->step_list);
		while ((step_ptr = list_next(itr))) {
			if(step_ptr->step_id ==
			   spec_info->search_info->int_data2) {
				j=0;
				while(step_ptr->node_inx[j] >= 0) {
					change_grid_color(
						popup_win->grid_button_list,
						step_ptr->node_inx[j],
						step_ptr->node_inx[j+1],
						step_ptr->step_id, false, 0);
					j += 2;
				}
				_layout_step_record(treeview,
						    step_ptr, update);
				found = 1;
				break;
			}
		}
		list_iterator_destroy(itr);
	}
	post_setup_popup_grid_list(popup_win);

	if(!found) {
		if(!popup_win->not_found) {
			char *temp = "JOB ALREADY FINISHED OR NOT FOUND\n";
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;

			/* only time this will be run so no update */
			model = gtk_tree_view_get_model(treeview);
			add_display_treestore_line(0,
						   GTK_TREE_STORE(model),
						   &iter,
						   temp, "");
			if(spec_info->search_info->int_data2 != NO_VAL)
				add_display_treestore_line(
					1,
					GTK_TREE_STORE(model),
					&iter,
					find_col_name(display_data_job,
						      SORTID_STATE),
					job_state_string(JOB_COMPLETE));
		}
		popup_win->not_found = true;
	} else {
		if(popup_win->not_found) {
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);

			goto need_refresh;
		}
	}
	gtk_widget_show_all(spec_info->display_widget);

finished:
	return;
}

extern void refresh_job(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_job(popup_win);
}

extern int get_new_info_job(job_info_msg_t **info_ptr,
			    int force)
{
	job_info_msg_t *new_job_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
	static uint16_t last_flags = 0;

	if(g_job_info_ptr && !force
	   && ((now - last) < working_sview_config.refresh_delay)) {
		if(*info_ptr != g_job_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_job_info_ptr;
		if(changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;

	if(working_sview_config.show_hidden)
		show_flags |= SHOW_ALL;
	if (g_job_info_ptr) {
		if(show_flags != last_flags)
			g_job_info_ptr->last_update = 0;
		error_code = slurm_load_jobs(g_job_info_ptr->last_update,
					     &new_job_ptr, show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_job_info_msg(g_job_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_job_ptr = g_job_info_ptr;
			changed = 0;
		}
	} else {
		new_job_ptr = NULL;
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr,
					     show_flags);
		changed = 1;
	}

	last_flags = show_flags;
	g_job_info_ptr = new_job_ptr;

	if(g_job_info_ptr && (*info_ptr != g_job_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_job_info_ptr;
end_it:
	return error_code;
}

extern int get_new_info_job_step(job_step_info_response_msg_t **info_ptr,
				 int force)
{
	job_step_info_response_msg_t *new_step_ptr = NULL;
	uint16_t show_flags = 0;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;

	if(g_step_info_ptr && !force
	   && ((now - last) < working_sview_config.refresh_delay)) {
		if(*info_ptr != g_step_info_ptr)
			error_code = SLURM_SUCCESS;
		*info_ptr = g_step_info_ptr;
		if(changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;

	/* This needs to always be like this even if you are only
	   looking for non-hidden jobs or you will get an error below.
	*/
	show_flags |= SHOW_ALL;
	if (g_step_info_ptr) {
		error_code = slurm_get_job_steps(g_step_info_ptr->last_update,
						 NO_VAL, NO_VAL, &new_step_ptr,
						 show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_job_step_info_response_msg(g_step_info_ptr);
			changed = 1;
		} else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_step_ptr = g_step_info_ptr;
			changed = 0;
		}
	} else {
		new_step_ptr = NULL;
		error_code = slurm_get_job_steps((time_t) NULL, NO_VAL, NO_VAL,
						 &new_step_ptr, show_flags);
		changed = 1;
	}

	g_step_info_ptr = new_step_ptr;

	if(g_step_info_ptr && (*info_ptr != g_step_info_ptr))
		error_code = SLURM_SUCCESS;

	*info_ptr = g_step_info_ptr;
end_it:
	return error_code;
}

extern GtkListStore *create_model_job(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	switch(type) {
	case SORTID_ACTION:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "None",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Cancel",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Suspend/Resume",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Checkpoint",
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Requeue",
				   -1);
		break;
	case SORTID_SHARED:
	case SORTID_CONTIGUOUS:
	case SORTID_REQUEUE:
	case SORTID_ROTATE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, type,
				   -1);
		break;
	case SORTID_CONNECTION:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Torus",
				   1, SORTID_CONNECTION,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Mesh",
				   1, SORTID_CONNECTION,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "NAV",
				   1, SORTID_CONNECTION,
				   -1);
		if(!(cluster_flags & CLUSTER_FLAG_BGL)) {
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter,
					   0, "HTC SMP",
					   1, SORTID_CONNECTION,
					   -1);
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter,
					   0, "HTC Dual",
					   1, SORTID_CONNECTION,
					   -1);
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter,
					   0, "HTC Virtual",
					   1, SORTID_CONNECTION,
					   -1);
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter,
					   0, "HTC Linux",
					   1, SORTID_CONNECTION,
					   -1);
		}
		break;
	default:
		break;
	}

	return model;
}

extern void admin_edit_job(GtkCellRendererText *cell,
			   const char *path_string,
			   const char *new_text,
			   gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	job_desc_msg_t *job_msg = xmalloc(sizeof(job_desc_msg_t));

	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;
	int stepid = NO_VAL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));

	if(!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	slurm_init_job_desc_msg(job_msg);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_JOBID, &job_msg->job_id,
			   column, &old_text,
			   -1);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_ALLOC, &stepid, -1);
	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = job_msg->job_id;
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
				   SORTID_POS, &job_msg->job_id, -1);
	}

	type = _set_job_msg(job_msg, new_text, column);
	if(global_edit_error)
		goto print_error;

	if(got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		admin_job(GTK_TREE_MODEL(treestore), &iter, temp, NULL);
		xfree(temp);
		goto no_input;
	}

	if(old_text && !strcmp(old_text, new_text)) {
		temp = g_strdup_printf("No change in value.");
	} else if(slurm_update_job(job_msg)
		  == SLURM_SUCCESS) {
		gtk_tree_store_set(treestore, &iter, column, new_text, -1);
		temp = g_strdup_printf("Job %d %s changed to %s",
				       job_msg->job_id,
				       type,
				       new_text);
	} else if(errno == ESLURM_DISABLED) {
		temp = g_strdup_printf(
			"Can only edit %s on pending jobs.", type);
	} else {
	print_error:
		temp = g_strdup_printf("Job %d %s can't be "
				       "set to %s",
				       job_msg->job_id,
				       type,
				       new_text);
	}

	display_edit_note(temp);
	g_free(temp);

no_input:
	slurm_free_job_desc_msg(job_msg);

	gtk_tree_path_free (path);
	g_free(old_text);
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_job(GtkTable *table, display_data_t *display_data)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static int view = -1;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int changed = 1;
	int j=0;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	ListIterator itr = NULL;
	GtkTreePath *path = NULL;

	/* reset */
	if(!table && !display_data) {
		if(display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		job_info_ptr = NULL;
		step_info_ptr = NULL;
		return;
	}

	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_job->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if((job_error_code = get_new_info_job(&job_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA){

	} else if (job_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

	if((step_error_code = get_new_info_job_step(&step_info_ptr,
						    force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA){
		if((!display_widget || view == ERROR_VIEW)
		   || (job_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
	} else if (step_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:

	info_list = _create_job_info_list(job_info_ptr, step_info_ptr,
					  changed, 0);
	if(!info_list)
		goto reset_curs;

	/* set up the grid */
	if(display_widget && GTK_IS_TREE_VIEW(display_widget)
	   && gtk_tree_selection_count_selected_rows(
		   gtk_tree_view_get_selection(
			   GTK_TREE_VIEW(display_widget)))) {
		GtkTreeViewColumn *focus_column = NULL;
		/* highlight the correct nodes from the last selection */
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(display_widget),
					 &path, &focus_column);
	}
	if(!path) {
		itr = list_iterator_create(info_list);
		while ((sview_job_info_ptr = list_next(itr))) {
			job_ptr = sview_job_info_ptr->job_ptr;
			j=0;
			while(job_ptr->node_inx[j] >= 0) {
				change_grid_color(grid_button_list,
						  job_ptr->node_inx[j],
						  job_ptr->node_inx[j+1],
						  sview_job_info_ptr->color_inx,
						  true, 0);
				j += 2;
			}
		}
		list_iterator_destroy(itr);
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
	} else
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_NODE_INX, SORTID_COLOR_INX,
			       grid_button_list);

	if(working_sview_config.grid_speedup) {
		gtk_widget_set_sensitive(GTK_WIDGET(main_grid_table), 0);
		gtk_widget_set_sensitive(GTK_WIDGET(main_grid_table), 1);
	}

	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if(!display_widget) {
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		/*set multiple capability here*/
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(GTK_TABLE(table),
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, display_data_job,
				 SORTID_CNT, SORTID_TIME_SUBMIT, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_job(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;

reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_job(popup_info_t *popup_win)
{
	int job_error_code = SLURM_SUCCESS;
	int step_error_code = SLURM_SUCCESS;
	static job_info_msg_t *job_info_ptr = NULL;
	static job_step_info_response_msg_t *step_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	List send_info_list = NULL;
	int changed = 1;
	int j=0, i=-1;
	sview_job_info_t *sview_job_info_ptr = NULL;
	job_info_t *job_ptr = NULL;
	ListIterator itr = NULL;
	char name[30], *uname = NULL;
	hostset_t hostset = NULL;
	int name_diff;

	if(!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_job, SORTID_CNT);

	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if((job_error_code =
	    get_new_info_job(&job_info_ptr, popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) {

	} else if (job_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);

		sprintf(error_char, "slurm_load_job: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(popup_win->table),
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

	if((step_error_code =
	    get_new_info_job_step(&step_info_ptr, popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) {
		if((!spec_info->display_widget
		    || spec_info->view == ERROR_VIEW)
		   || (job_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
	} else if (step_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_job_step: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}
display_it:
	info_list = _create_job_info_list(job_info_ptr, step_info_ptr,
					  changed, 1);
	if(!info_list)
		return;

	if(spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}

	if(spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data,
					    &popup_win->grid_button_list);
		/*set multiple capability here*/
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_TIME_SUBMIT, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_job(info_list, popup_win);
		goto end_it;
	}


	/* just linking to another list, don't free the inside, just
	   the list */
	send_info_list = list_create(NULL);
	itr = list_iterator_create(info_list);
	i = -1;
	while ((sview_job_info_ptr = list_next(itr))) {
		i++;
		job_ptr = sview_job_info_ptr->job_ptr;
		switch(spec_info->type) {
		case JOB_PAGE:
			switch(search_info->search_type) {
			case SEARCH_JOB_ID:
				if(search_info->int_data
				   == NO_VAL) {
					if(!search_info->gchar_data)
						continue;
					_convert_char_to_job_and_step(
						search_info->gchar_data,
						&search_info->int_data,
						&search_info->int_data2);
				}
				if(job_ptr->job_id != search_info->int_data) {
					continue;
				}
				/* if we ever want to display just the step
				   this is where we would do it */
/* 				if(spec_info->search_info->int_data2 */
/* 				   == NO_VAL) */
/* 				break; */
/* 			step_itr = list_iterator_create( */
/* 				sview_job_info->step_list); */
/* 			while ((step_ptr = list_next(itr))) { */
/* 				if(step_ptr->step_id  */
/* 				   == spec_info->search_info->int_data2) { */
/* 					break; */
/* 				} */
/* 			} */
				break;
			case SEARCH_JOB_USER:
				if(!search_info->gchar_data)
					continue;
				uname = uid_to_string(job_ptr->user_id);
				name_diff = strcmp(uname,
						   search_info->gchar_data);
				xfree(uname);
				if(name_diff)
					continue;
				break;
			case SEARCH_JOB_STATE:
				if(search_info->int_data == NO_VAL)
					continue;

				if(job_ptr->job_state != search_info->int_data)
					continue;
				break;
			default:
				break;
			}
			break;
		case PART_PAGE:
			if(strcmp(search_info->gchar_data,
				  job_ptr->partition))
				continue;
			break;
		case RESV_PAGE:
			if(strcmp(search_info->gchar_data,
				  job_ptr->resv_name))
				continue;
			break;
		case BLOCK_PAGE:
			select_g_select_jobinfo_sprint(
				job_ptr->select_jobinfo,
				name,
				sizeof(name),
				SELECT_PRINT_BG_ID);
			if(strcmp(search_info->gchar_data, name))
				continue;
			break;
		case NODE_PAGE:
			if(!job_ptr->nodes)
				continue;

			if(!(hostset = hostset_create(search_info->gchar_data)))
				continue;
			if(!hostset_intersects(hostset, job_ptr->nodes)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);
			break;
		default:
			continue;
		}

		list_push(send_info_list, sview_job_info_ptr);
		j=0;
		while(job_ptr->node_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				job_ptr->node_inx[j],
				job_ptr->node_inx[j+1],
				sview_job_info_ptr->color_inx,
				true, 0);
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_job(send_info_list,
			 GTK_TREE_VIEW(spec_info->display_widget));

	list_destroy(send_info_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	return;
}

extern void set_menus_job(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_job, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_job);
		break;
	case ROW_LEFT_CLICKED:
		highlight_grid(tree_view, SORTID_NODE_INX,
			       SORTID_COLOR_INX, button_list);
		break;
	case FULL_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("job error getting iter from model\n");
			break;
		}

		popup_all_job(model, &iter, INFO_PAGE);

		break;
	}
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_job(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	int jobid = NO_VAL;
	int stepid = NO_VAL;
	GError *error = NULL;
	int i=0;
	char *type;

	if(cluster_flags & CLUSTER_FLAG_BG)
		type = "Base partition";
	else
		type = "Node";

	gtk_tree_model_get(model, iter, SORTID_JOBID, &jobid, -1);
	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);

	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	switch(id) {
	case PART_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100, "Partition with job %d", jobid);
		else
			snprintf(title, 100, "Partition with job %d.%d",
				 jobid, stepid);
		break;
	case RESV_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100, "Reservation with job %d", jobid);
		else
			snprintf(title, 100, "Reservation with job %d.%d",
				 jobid, stepid);
		break;
	case NODE_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100,
				 "%s(s) running job %d", type, jobid);
		else
			snprintf(title, 100, "%s(s) running job %d.%d",
				 type, jobid, stepid);
		break;
	case BLOCK_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100, "Block with job %d", jobid);
		else
			snprintf(title, 100, "Block with job %d.%d",
				 jobid, stepid);
		break;
	case INFO_PAGE:
		if(stepid == NO_VAL)
			snprintf(title, 100, "Full info for job %d", jobid);
		else
			snprintf(title, 100, "Full info for job %d.%d",
				 jobid, stepid);
		break;
	default:
		g_print("jobs got id %d\n", id);
	}

	itr = list_iterator_create(popup_list);
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info)
			if(!strcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if(!popup_win) {
		if(id == INFO_PAGE)
			popup_win = create_popup_info(id, JOB_PAGE, title);
		else
			popup_win = create_popup_info(JOB_PAGE, id, title);
	} else {
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	   the current node_inx.
	*/
	popup_win->model = model;
	popup_win->iter = *iter;
	popup_win->node_inx_id = SORTID_NODE_INX;

	switch(id) {
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		if(cluster_flags & CLUSTER_FLAG_BG) {
			gtk_tree_model_get(model, iter,
					   SORTID_SMALL_BLOCK, &i, -1);
			if(i) {
				i=0;
				/* strip off the ionodes part */
				while(name[i]) {
					if(name[i] == '[') {
						name[i] = '\0';
						break;
					}
					i++;
				}
			}
		}
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case PART_PAGE:
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case RESV_PAGE:
		gtk_tree_model_get(model, iter, SORTID_RESV_NAME, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case BLOCK_PAGE:
		if(cluster_flags & CLUSTER_FLAG_BG) {
			gtk_tree_model_get(model, iter,
					   SORTID_BLOCK, &name, -1);
			popup_win->spec_info->search_info->gchar_data = name;
		}
		break;
	case SUBMIT_PAGE:
		break;
	case INFO_PAGE:
		popup_win->spec_info->search_info->int_data = jobid;
		popup_win->spec_info->search_info->int_data2 = stepid;
		break;

	default:
		g_print("jobs got %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error)) {
		g_printerr ("Failed to create part popup thread: %s\n",
			    error->message);
		return;
	}
}

static void process_foreach_list (jobs_foreach_common_t *jobs_foreach_common)
{
	int jobid;
	int state;
	int stepid;
	uint16_t signal = SIGKILL;
	int response = 0;
	char *tmp_char_ptr = "";
	jobs_foreach_t *job_foreach = NULL;
	ListIterator itr = NULL;

	if (jobs_foreach_common->edit_type == EDIT_SIGNAL) {
		const gchar *entry_txt = gtk_entry_get_text(
			GTK_ENTRY(jobs_foreach_common->entry));
		signal = _xlate_signal_name(entry_txt);
		if(signal == (uint16_t)NO_VAL) {
			tmp_char_ptr = g_strdup_printf(
				"%s is not a valid signal.",
				entry_txt);
			display_edit_note(tmp_char_ptr);
			g_free(tmp_char_ptr);
			goto end_it;
		}
	}

	itr = list_iterator_create(foreach_list);
	while ((job_foreach = list_next(itr))) {
		/*stop processing remaining jobs on any error*/
		if (global_error_code)
			break;

		jobid = job_foreach->job_id;
		stepid = job_foreach->step_id;
		state = job_foreach->state;

		switch(jobs_foreach_common->edit_type) {
		case EDIT_SIGNAL:
			/* fall through to the cancel now the signal
			 * is set (Default is SIGKILL from above for
			 * just a regular cancel).
			 */
		case EDIT_CANCEL:
			if(stepid == NO_VAL)
				global_error_code =
					_cancel_job_id(jobid, signal);
			else
				global_error_code =
					_cancel_step_id(jobid,
							stepid, signal);
			break;
		case EDIT_REQUEUE:
			response = slurm_requeue(jobid);

			if(response) {
				/* stop rest of jobs */
				global_error_code = response;
				tmp_char_ptr = g_strdup_printf(
					"Error happened trying "
					"to requeue job %u: %s",
					jobid, slurm_strerror(response));
				display_edit_note(tmp_char_ptr);
				g_free(tmp_char_ptr);
			}
			break;
		case EDIT_SUSPEND:
			//note: derive state from job_foreach..
			if(state == JOB_SUSPENDED)
				response = slurm_resume(jobid);
			else
				response = slurm_suspend(jobid);
			if(!response) {
				/* stop rest of jobs */
				global_error_code = response;
				tmp_char_ptr = g_strdup_printf(
					"Error happened trying to "
					"SUSPEND/RESUME job %u.",
					jobid);
				display_edit_note(tmp_char_ptr);
				g_free(tmp_char_ptr);
			}
			break;
		case EDIT_EDIT:
			/* note: this case only applies to
			   single jobs, but still gets
			   processed here */
			if(got_edit_signal)
				goto end_it;

			if(global_edit_error)
				tmp_char_ptr = global_edit_error_msg;
			else if(!global_send_update_msg) {
				tmp_char_ptr = g_strdup_printf(
					"No change detected.");
			} else if(slurm_update_job(
					  jobs_foreach_common->job_msg)
				  == SLURM_SUCCESS) {
				tmp_char_ptr = g_strdup_printf(
					"Job %u updated successfully",
					jobid);
			} else if(errno == ESLURM_DISABLED) {
				tmp_char_ptr = g_strdup_printf(
					"Can't edit that part of "
					"non-pending job %u.",
					jobid);
			} else {
				tmp_char_ptr = g_strdup_printf(
					"Problem updating job %u.",
					jobid);
			}
			display_edit_note(tmp_char_ptr);
			g_free(tmp_char_ptr);
			break;
		default:
			break;

		}/*end switch*/
	} /*spin thru selected jobs*/

	if(global_edit_error || global_error_code)
		goto end_it;

	switch(jobs_foreach_common->edit_type) {
	case EDIT_SIGNAL:
		tmp_char_ptr = g_strdup_printf(
			"Signal successfully sent to job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_CANCEL:
		tmp_char_ptr = g_strdup_printf(
			"Cancel successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_REQUEUE:
		tmp_char_ptr = g_strdup_printf(
			"Requeue successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	case EDIT_SUSPEND:
		tmp_char_ptr = g_strdup_printf(
			"SUSPEND/RESUME action successful for job(s)%s",
			stacked_job_list);
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
		break;
	default:
		break;

	}/*end switch*/

end_it:
	xfree(stacked_job_list);

} /*process_foreach_list ^^^*/

static void selected_foreach_build_list(GtkTreeModel  *model,
					GtkTreePath   *path,
					GtkTreeIter   *iter,
					gpointer       userdata)
{
	uint32_t jobid = NO_VAL;
	uint32_t stepid = NO_VAL;
	int state;
	jobs_foreach_t *fe_ptr = NULL;

	gtk_tree_model_get(model, iter, SORTID_JOBID, &jobid, -1);
	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);

	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	gtk_tree_model_get(model, iter, SORTID_STATE_NUM, &state, -1);

	/* alc mem for individual job processor target */
	fe_ptr = xmalloc(sizeof(jobs_foreach_t));
	fe_ptr->job_id = jobid;
	fe_ptr->step_id = stepid;
	fe_ptr->state = state;

	list_append(foreach_list, fe_ptr); /* stuff target away*/

	if(stacked_job_list)
		xstrcat(stacked_job_list, ", ");
	else
		xstrcat(stacked_job_list, ": ");

	if(stepid == NO_VAL)
		xstrfmtcat(stacked_job_list, "%u", jobid);
	else
		xstrfmtcat(stacked_job_list, "%u.%u", jobid, stepid);
}
/*selected_foreach_build_list ^^^*/

extern void admin_job(GtkTreeModel *model, GtkTreeIter *iter,
		      char *type, GtkTreeView *treeview)
{
	int jobid = NO_VAL;
	int stepid = NO_VAL;
	int response = 0;
	char tmp_char[255];
	int edit_type = 0;
	int row_count=0;
	job_desc_msg_t *job_msg = xmalloc(sizeof(job_desc_msg_t));
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);

	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	if (treeview)
		row_count = gtk_tree_selection_count_selected_rows(
			gtk_tree_view_get_selection(treeview));

	gtk_tree_model_get(model, iter, SORTID_JOBID, &jobid, -1);
	gtk_tree_model_get(model, iter, SORTID_ALLOC, &stepid, -1);
	if(stepid)
		stepid = NO_VAL;
	else {
		stepid = jobid;
		gtk_tree_model_get(model, iter, SORTID_POS, &jobid, -1);
	}

	slurm_init_job_desc_msg(job_msg);

	if(!strcasecmp("Signal", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		entry = create_entry();
		label = gtk_label_new("Signal?");
		edit_type = EDIT_SIGNAL;
	} else if(!strcasecmp("Requeue", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to requeue these job(s)?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_REQUEUE;
	} else if(!strcasecmp("Cancel", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		if(stepid != NO_VAL)
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to cancel "
				 "these job step(s)?");
		else
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to cancel "
				 "these job(s)?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_CANCEL;
	} else if(!strcasecmp("Suspend/Resume", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		if(stepid != NO_VAL)
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to toggle "
				 "suspend/resume on these job steps?");
		else
			snprintf(tmp_char, sizeof(tmp_char),
				 "Are you sure you want to toggle "
				 "suspend/resume on these jobs?");
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_SUSPEND;
	} else {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char),
			 "Editing job %u think before you type",
			 jobid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_EDIT;
		job_msg->job_id = jobid;
		entry = _admin_full_edit_job(job_msg, model, iter);
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, FALSE, FALSE, 0);
	if(entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   entry, TRUE, TRUE, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		jobs_foreach_common_t job_foreach_common;
		global_error_code = SLURM_SUCCESS;
		/* setup params that applies to ALL selections */
		memset(&job_foreach_common, 0, sizeof(jobs_foreach_common_t));
		job_foreach_common.type = type;
		job_foreach_common.edit_type = edit_type;
		job_foreach_common.entry = entry;

		/* pass this ptr for single job selections */
		job_foreach_common.job_msg = job_msg;

		/* create a list to stack the selected jobs */
		foreach_list = list_create(_destroy_jobs_foreach);
		/* build array of job(s) to process */
		if(treeview)
			gtk_tree_selection_selected_foreach(
				gtk_tree_view_get_selection(treeview),
				selected_foreach_build_list, NULL);
		else
			selected_foreach_build_list(model, NULL, iter, NULL);
		/* determine what to do with them/it */
		process_foreach_list(&job_foreach_common); /*go do them*/
		list_destroy(foreach_list);
	}/*response OK ^^*/
	/* switch back to standard cursor*/

	global_entry_changed = 0;
	slurm_free_job_desc_msg(job_msg);
	gtk_widget_destroy(popup);
	if(got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		admin_job(model, iter, type, treeview);
		xfree(type);
	}
	return;
}

extern void cluster_change_job()
{
	display_data_t *display_data = display_data_job;
	while(display_data++) {
		if(display_data->id == -1)
			break;
		if(cluster_flags & CLUSTER_FLAG_AIX) {
			switch(display_data->id) {
			case SORTID_NETWORK:
				display_data->name = "Network";
				break;
			}

		} else {
			switch(display_data->id) {
			case SORTID_NETWORK:
				display_data->name = NULL;
				break;
			}
		}

		if(cluster_flags & CLUSTER_FLAG_CRAYXT) {
			switch(display_data->id) {
			case SORTID_ALPS_RESV_ID:
				display_data->name = "ALPS Resv ID";
				break;
			}
		} else {
			switch(display_data->id) {
			case SORTID_ALPS_RESV_ID:
				display_data->name = NULL;
				break;
			}
		}

		if(cluster_flags & CLUSTER_FLAG_BG) {
			switch(display_data->id) {
			case SORTID_BLOCK:
				display_data->name = "BG Block";
				break;
			case SORTID_GEOMETRY:
				display_data->name = "Geometry";
				break;
			case SORTID_ROTATE:
				display_data->name = "Rotate";
				break;
			case SORTID_CONNECTION:
				display_data->name = "Connection";
				break;
			case SORTID_IMAGE_MLOADER:
				display_data->name = "Image Mloader";
				break;
			case SORTID_NODELIST:
				display_data->name = "BP List";
				break;
			case SORTID_NODELIST_EXC:
				display_data->name = "BP List Excluded";
				break;
			case SORTID_NODELIST_REQ:
				display_data->name = "BP List Requested";
				break;
			default:
				break;
			}
			if(cluster_flags & CLUSTER_FLAG_BGL) {
				switch(display_data->id) {
				case SORTID_IMAGE_BLRTS:
					display_data->name = "Image Blrts";
					break;
				case SORTID_IMAGE_LINUX:
					display_data->name = "Image Linux";
					break;
				case SORTID_IMAGE_RAMDISK:
					display_data->name = "Image Ramdisk";
					break;
				}
			} else {
				switch(display_data->id) {
				case SORTID_IMAGE_BLRTS:
					display_data->name = NULL;
					break;
				case SORTID_IMAGE_LINUX:
					display_data->name = "Image Cnload";
					break;
				case SORTID_IMAGE_RAMDISK:
					display_data->name = "Image Ioload";
					break;
				}
			}
		} else {
			switch(display_data->id) {
			case SORTID_BLOCK:
				display_data->name = NULL;
				break;
			case SORTID_GEOMETRY:
				display_data->name = NULL;
				break;
			case SORTID_ROTATE:
				display_data->name = NULL;
				break;
			case SORTID_CONNECTION:
				display_data->name = NULL;
				break;
			case SORTID_IMAGE_MLOADER:
				display_data->name = NULL;
				break;
			case SORTID_NODELIST:
				display_data->name = "NodeList";
				break;
			case SORTID_NODELIST_EXC:
				display_data->name = "NodeList Excluded";
				break;
			case SORTID_NODELIST_REQ:
				display_data->name = "NodeList Requested";
				break;
			case SORTID_IMAGE_BLRTS:
				display_data->name = NULL;
				break;
			case SORTID_IMAGE_LINUX:
				display_data->name = NULL;
				break;
			case SORTID_IMAGE_RAMDISK:
				display_data->name = NULL;
				break;
			}
		}
	}
	display_data = options_data_job;
	while(display_data++) {
		if(display_data->id == -1)
			break;

		if(cluster_flags & CLUSTER_FLAG_BG) {
			switch(display_data->id) {
			case BLOCK_PAGE:
				display_data->name = "Blocks";
				break;
			case NODE_PAGE:
				display_data->name = "Base Partitions";
				break;
			}
		} else {
			switch(display_data->id) {
			case BLOCK_PAGE:
				display_data->name = NULL;
				break;
			case NODE_PAGE:
				display_data->name = "Nodes";
				break;
			}
		}
	}
	get_info_job(NULL, NULL);
}