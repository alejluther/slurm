/*****************************************************************************\
 *  cluster_functions.c - functions dealing with clusters in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "sacctmgr.h"
#include "print.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_cluster_cond_t *cluster_cond)
{
	int i;
	int set = 0;
	int end = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if(!end) {
			addto_char_list(cluster_cond->cluster_list, argv[i]);
			set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(cluster_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else {
			printf(" Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_cluster_rec_t *cluster)
{
	int i;
	int set = 0;
	int end = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		} else if(!end) {
			printf(" Bad format on %s: End your option with "
			       "an '=' sign\n", argv[i]);			
		} else if (strncasecmp (argv[i], "FairShare", 1) == 0) {
			cluster->default_fairshare = atoi(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs", 4) == 0) {
			cluster->default_max_jobs = atoi(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes", 4) == 0) {
			cluster->default_max_nodes_per_job = atoi(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxWall", 4) == 0) {
			cluster->default_max_wall_duration_per_job =
				atoi(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs=", 11) == 0) {
			cluster->default_max_cpu_secs_per_job =
				atoi(argv[i]+end);
			set = 1;
		} else {
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}
	(*start) = i;

	return set;

}

extern int sacctmgr_add_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = NULL;
	uint32_t fairshare = -2; 
	uint32_t max_cpu_secs_per_job = -2;
	uint32_t max_jobs = -2; 
	uint32_t max_nodes_per_job = -2;
	uint32_t max_wall_duration_per_job = -2;
	int limit_set = 0;
	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end) {
			addto_char_list(name_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "FairShare", 1) == 0) {
			fairshare = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs4", 4) == 0) {
			max_cpu_secs_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs=", 4) == 0) {
			max_jobs = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes", 4) == 0) {
			max_nodes_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall", 4) == 0) {
			max_wall_duration_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(name_list, argv[i]+end);
		} else {
			printf(" Unknown option: %s\n", argv[i]);
		}		
	}

	if(!list_count(name_list)) {
		list_destroy(name_list);
		printf(" Need name of cluster to add.\n"); 
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;
		char *name = NULL;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = name_list;

		temp_list = acct_storage_g_get_clusters(db_conn, &cluster_cond);
		if(!temp_list) {
			printf(" Problem getting clusters from database.  "
		   	    "Contact your admin.\n");
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(name_list);
		itr = list_iterator_create(temp_list);
		while((name = list_next(itr_c))) {
			acct_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if(!strcasecmp(cluster_rec->name, name))
					break;
			}
			if(cluster_rec) {
				printf(" This cluster %s already exists.  "
				       "Not adding.\n", name);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		list_destroy(temp_list);
		if(!list_count(name_list)) {
			list_destroy(name_list);
			return SLURM_ERROR;
		}
	}

	printf(" Adding Cluster(s)\n");
	cluster_list = list_create(destroy_acct_cluster_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		cluster = xmalloc(sizeof(acct_cluster_rec_t));
		cluster->name = xstrdup(name);
		list_append(cluster_list, cluster);

		printf("  Name          = %s\n", cluster->name);

		cluster->default_fairshare = fairshare;		
		cluster->default_max_cpu_secs_per_job = max_cpu_secs_per_job;
		cluster->default_max_jobs = max_jobs;
		cluster->default_max_nodes_per_job = max_nodes_per_job;
		cluster->default_max_wall_duration_per_job = 
			max_wall_duration_per_job;
	}
	list_iterator_destroy(itr);
	list_destroy(name_list);

	if(limit_set) {
		printf(" User Defaults\n");
		if((int)fairshare != -2)
			printf("  Fairshare       = %u\n", fairshare);
		if((int)max_cpu_secs_per_job != -2)
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
		if((int)max_jobs != -2)
			printf("  MaxJobs         = %u\n", max_jobs);
		if((int)max_nodes_per_job != -2)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		if((int)max_wall_duration_per_job != -2)
			printf("  MaxWall         = %u\n",
			       max_wall_duration_per_job);
	}

	if(!list_count(cluster_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	}

	notice_thread_init();
	rc = acct_storage_g_add_clusters(db_conn, my_uid, cluster_list);
	notice_thread_fini();
	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		printf(" error: problem adding clusters\n");
	}
end_it:
	list_destroy(cluster_list);
	
	return rc;
}

extern int sacctmgr_list_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	List cluster_list;
	int i=0;
	ListIterator itr = NULL;
	acct_cluster_rec_t *cluster = NULL;
	print_field_t name_field;
/* 	print_field_t fs_field; */
	List print_fields_list; /* types are of print_field_t */

	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	_set_cond(&i, argc, argv, cluster_cond);
	
	cluster_list = acct_storage_g_get_clusters(db_conn, cluster_cond);
	destroy_acct_cluster_cond(cluster_cond);
	
	if(!cluster_list) 
		return SLURM_ERROR;
	
	print_fields_list = list_create(NULL);

	name_field.name = "Name";
	name_field.len = 10;
	name_field.print_routine = print_str;
	list_append(print_fields_list, &name_field);

/* 	fs_field.name = "FS"; */
/* 	fs_field.len = 4; */
/* 	fs_field.print_routine = print_str; */
/* 	list_append(print_fields_list, &fs_field); */	
	
	itr = list_iterator_create(cluster_list);
	print_header(print_fields_list);
	
	while((cluster = list_next(itr))) {
		print_str(VALUE, &name_field, cluster->name);
		//print_int(VALUE, &fs_field, cluster->default_fairshare);
		printf("\n");
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(cluster_list);
	list_destroy(print_fields_list);
	return rc;
}

extern int sacctmgr_modify_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	acct_cluster_rec_t *cluster = xmalloc(sizeof(acct_cluster_rec_t));
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	List cluster_list = NULL;
	int cond_set = 0, rec_set = 0;
	List ret_list = NULL;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);

	cluster->default_fairshare = -2; 
	cluster->default_max_cpu_secs_per_job = -2;
	cluster->default_max_jobs = -2; 
	cluster->default_max_nodes_per_job = -2;
	cluster->default_max_wall_duration_per_job = -2;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, cluster_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, cluster))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, cluster_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_acct_cluster_rec(cluster);
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_cluster_rec(cluster);
			destroy_acct_cluster_cond(cluster_cond);
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	if(rec_set) 
		printf(" User Defaults  =\n");
	if((int)cluster->default_fairshare != -2)
		printf("  Fairshare     = %u\n", cluster->default_fairshare);

	if((int)cluster->default_max_cpu_secs_per_job != -2)
		printf("  MaxCPUSecs    = %u\n",
		       cluster->default_max_cpu_secs_per_job);
	if((int)cluster->default_max_jobs != -2)
		printf("  MaxJobs       = %u\n", cluster->default_max_jobs);
	if((int)cluster->default_max_nodes_per_job != -2)
		printf("  MaxNodes      = %u\n",
		       cluster->default_max_nodes_per_job);
	if((int)cluster->default_max_wall_duration_per_job != -2)
		printf("  MaxWall       = %u\n",
		       cluster->default_max_wall_duration_per_job);

	cluster_list = list_create(destroy_acct_cluster_rec);
	list_append(cluster_list, cluster);
	notice_thread_init();
	ret_list = acct_storage_g_modify_clusters(
		db_conn, my_uid, cluster_cond, cluster);
	notice_thread_fini();
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modifying clusters...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if(ret_list) {
		printf(" Nothing modified\n");
	} else {
		printf(" Error with request\n");
		rc = SLURM_ERROR;
	}

	if(ret_list)
		list_destroy(ret_list);

	destroy_acct_cluster_cond(cluster_cond);
	destroy_acct_cluster_rec(cluster);

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	int i=0;
	List ret_list = NULL;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	
	if(!_set_cond(&i, argc, argv, cluster_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	}

	if(!list_count(cluster_cond->cluster_list)) {
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = acct_storage_g_remove_clusters(
		db_conn, my_uid, cluster_cond);
	notice_thread_fini();

	destroy_acct_cluster_cond(cluster_cond);

	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Deleting clusters...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if(ret_list) {
		printf(" Nothing deleted\n");
	} else {
		printf(" Error with request\n");
		rc = SLURM_ERROR;
	}

	if(ret_list)
		list_destroy(ret_list);

	return rc;
}