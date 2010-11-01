/*****************************************************************************\
 *  bg_job_place.c - blue gene job placement (e.g. base block selection)
 *  functions.
 *
 *  $Id$ 
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
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

#include <grp.h>
#include <pwd.h>

#include "src/common/node_select.h"
#include "src/slurmctld/trigger_mgr.h"
#include "bluegene.h"

#define _DEBUG 0
#define MAX_GROUPS 128

#define SWAP(a,b,t)	\
_STMT_START {		\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END

static int  _find_best_block_match(struct job_record* job_ptr,
				   bitstr_t* slurm_block_bitmap, uint32_t min_nodes, 
				   uint32_t max_nodes, uint32_t req_nodes,
				   int spec, bg_record_t** found_bg_record,
				   bool test_only);
static int  _get_user_groups(uint32_t user_id, uint32_t group_id, 
			     gid_t *groups, int max_groups, int *ngroups);
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt);
static int  _test_image_perms(char *image_name, List image_list, 
			      struct job_record* job_ptr);

/* Rotate a 3-D geometry array through its six permutations */
static void _rotate_geo(uint16_t *req_geometry, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
		case 0:		/* ABC -> ACB */
		case 2:		/* CAB -> CBA */
		case 4:		/* BCA -> BAC */
			SWAP(req_geometry[Y], req_geometry[Z], tmp);
			break;
		case 1:		/* ACB -> CAB */
		case 3:		/* CBA -> BCA */
		case 5:		/* BAC -> ABC */
			SWAP(req_geometry[X], req_geometry[Y], tmp);
			break;
	}
}

pthread_mutex_t create_dynamic_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Get a list of groups associated with a specific user_id
 * Return 0 on success, -1 on failure
 */
static int _get_user_groups(uint32_t user_id, uint32_t group_id, 
			    gid_t *groups, int max_groups, int *ngroups)
{
	struct passwd pwd, *results;
	char *buffer;
	static size_t buf_size = 0;
	int rc;

	if (!buf_size && ((buf_size = sysconf(_SC_GETPW_R_SIZE_MAX)) < 0)) {
		error("sysconf(_SC_GETPW_R_SIZE_MAX)");
		return -1;
	}
	buffer = xmalloc(buf_size);
	rc = getpwuid_r((uid_t) user_id, &pwd, buffer, buf_size, &results);
	if (rc != 0) {
		error("getpwuid_r(%u): %m", user_id);
		xfree(buffer);
		return -1;
	}
	*ngroups = max_groups;
	rc = getgrouplist(pwd.pw_name, (gid_t) group_id, groups, ngroups);
	xfree(buffer);
	if (rc < 0) {
		error("getgrouplist(%s): %m", pwd.pw_name);
		return -1;
	}
	*ngroups = rc;

	return 0;
}

/*
 * Determine if the job has permission to use the identified image
 */
static int _test_image_perms(char *image_name, List image_list, 
			     struct job_record* job_ptr)
{
	int allow = 0, i, rc;
	ListIterator itr;
	ListIterator itr2;
	image_t *image = NULL;
	image_group_t *image_group = NULL;

	/* Cache group information for most recently checked user */
	static gid_t groups[MAX_GROUPS];
	static int ngroups = -1;
	static int32_t cache_user = -1;

	itr = list_iterator_create(image_list);
	while ((image = list_next(itr))) {
		if (!strcasecmp(image->name, image_name) ||
		    !strcasecmp(image->name, "*")) {
			if (image->def) {
				allow = 1;
				break;
			}
			if (!image->groups ||
			    !list_count(image->groups)) {
				allow = 1;
				break;
			}
			if (job_ptr->user_id != cache_user) {
				rc = _get_user_groups(job_ptr->user_id, 
						      job_ptr->group_id,
						      groups, 
						      MAX_GROUPS, &ngroups);
				if (rc)		/* Failed to get groups */
					break;
				cache_user = job_ptr->user_id;
			}
			itr2 = list_iterator_create(image->groups);
			while ((allow == 0) &&
			       (image_group = list_next(itr2))) {
				for (i=0; i<ngroups; i++) {
					if (image_group->gid
					    == groups[i]) {
						allow = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr2);
			if (allow)
				break;	
		}
	}
	list_iterator_destroy(itr);

	return allow;
}

/*
 * finds the best match for a given job request 
 * 
 * IN - int spec right now holds the place for some type of
 * specification as to the importance of certain job params, for
 * instance, geometry, type, size, etc.
 * 
 * OUT - block_id of matched block, NULL otherwise
 * returns 1 for error (no match)
 * 
 */
static int _find_best_block_match(struct job_record* job_ptr, 
				  bitstr_t* slurm_block_bitmap,
				  uint32_t min_nodes, uint32_t max_nodes,
				  uint32_t req_nodes, int spec,
				  bg_record_t** found_bg_record, 
				  bool test_only)
{
	ListIterator itr;
	ListIterator itr2;
	bg_record_t *record = NULL;
	bg_record_t *found_record = NULL;
	uint16_t req_geometry[BA_SYSTEM_DIMENSIONS];
	uint16_t start[BA_SYSTEM_DIMENSIONS];
	uint16_t conn_type, rotate, target_size = 0;
	uint32_t req_procs = job_ptr->num_procs;
	uint32_t proc_cnt;
	ba_request_t request; 
	int i;
	int rot_cnt = 0;
	int created = 0;
	int allow = 0;
	int check_image = 1;
	uint32_t max_procs = NO_VAL;
	List lists_of_lists = NULL;
	List temp_list = NULL;
	char tmp_char[256];
	bitstr_t* tmp_bitmap = NULL;
	int start_req = 0;
	static int total_cpus = 0;
	char *blrtsimage = NULL;        /* BlrtsImage for this request */
	char *linuximage = NULL;        /* LinuxImage for this request */
	char *mloaderimage = NULL;      /* mloaderImage for this request */
	char *ramdiskimage = NULL;      /* RamDiskImage for this request */
	int rc = SLURM_SUCCESS;

	if(!total_cpus)
		total_cpus = DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z] 
			* procs_per_node;

	if(req_nodes > max_nodes) {
		error("can't run this job max bps is %u asking for %u",
		      max_nodes, req_nodes);
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&block_state_mutex);
	if(!test_only && req_procs > num_unused_cpus) {
		debug2("asking for %u I only got %d", 
		       req_procs, num_unused_cpus);
		slurm_mutex_unlock(&block_state_mutex);
		return SLURM_ERROR;
	}
	slurm_mutex_unlock(&block_state_mutex);
	if(!bg_list) {
		error("_find_best_block_match: There is no bg_list");
		return SLURM_ERROR;
	}
	
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_START, &start);
		
	if(start[X] != (uint16_t)NO_VAL)
		start_req = 1;

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_CONN_TYPE, &conn_type);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_GEOMETRY, &req_geometry);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_ROTATE, &rotate);
	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MAX_PROCS, &max_procs);

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_BLRTS_IMAGE, &blrtsimage);
	if (blrtsimage) {
		allow = _test_image_perms(blrtsimage, bg_blrtsimage_list, 
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use BlrtsImage %s",
			      job_ptr->user_id, job_ptr->group_id, blrtsimage);
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_LINUX_IMAGE, &linuximage);
	if (linuximage) {
		allow = _test_image_perms(linuximage, bg_linuximage_list, 
					  job_ptr);
		if (!allow) {
			error("User %u:%u is not allowed to use LinuxImage %s",
			      job_ptr->user_id, job_ptr->group_id, linuximage);
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_MLOADER_IMAGE, &mloaderimage);
	if (mloaderimage) {
		allow = _test_image_perms(mloaderimage, bg_mloaderimage_list, 
					  job_ptr);
		if(!allow) {
			error("User %u:%u is not allowed "
			      "to use MloaderImage %s",
			      job_ptr->user_id, job_ptr->group_id, 
			      mloaderimage);
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	select_g_get_jobinfo(job_ptr->select_jobinfo,
			     SELECT_DATA_RAMDISK_IMAGE, &ramdiskimage);
	if (ramdiskimage) {
		allow = _test_image_perms(ramdiskimage, bg_ramdiskimage_list, 
					  job_ptr);
		if(!allow) {
			error("User %u:%u is not allowed "
			      "to use RamDiskImage %s",
			      job_ptr->user_id, job_ptr->group_id, 
			      ramdiskimage);
			rc = SLURM_ERROR;
			goto end_it;
		}
	}
	
	if(req_geometry[X] != 0 && req_geometry[X] != (uint16_t)NO_VAL) {
		target_size = 1;
		for (i=0; i<BA_SYSTEM_DIMENSIONS; i++)
			target_size *= (uint16_t)req_geometry[i];
		if(target_size != min_nodes) {
			debug2("min_nodes not set correctly %u should be %u "
			      "from %u%u%u",
			      min_nodes, target_size, 
			      req_geometry[X],
			      req_geometry[Y],
			      req_geometry[Z]);
			min_nodes = target_size;
		}
		if(!req_nodes)
			req_nodes = req_nodes;
	}
	if (target_size == 0) {	/* no geometry specified */
		if(job_ptr->details->req_nodes 
		   && !start_req) {
			bg_record_t *tmp_record = NULL;
			char *tmp_nodes= job_ptr->details->req_nodes;
			int len = strlen(tmp_nodes);
			
			i = 0;
			while(i<len 
			      && tmp_nodes[i] != '[' 
			      && (tmp_nodes[i] < '0' || tmp_nodes[i] > 'Z'
				  || (tmp_nodes[i] > '9'
				      && tmp_nodes[i] < 'A')))
				i++;
			
			if(i<len) {
				len -= i;
				tmp_record = xmalloc(sizeof(bg_record_t));
				tmp_record->bg_block_list =
					list_create(destroy_ba_node);
				slurm_conf_lock();
				len += strlen(slurmctld_conf.node_prefix)+1;
				tmp_record->nodes = xmalloc(len);
				
				snprintf(tmp_record->nodes,
					 len,
					 "%s%s", 
					 slurmctld_conf.node_prefix, 
					 tmp_nodes+i);
				slurm_conf_unlock();
			
				process_nodes(tmp_record);
				for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) {
					req_geometry[i] = tmp_record->geo[i];
					start[i] = tmp_record->start[i];
				}
				destroy_bg_record(tmp_record);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_GEOMETRY, 
						     &req_geometry);
				select_g_set_jobinfo(job_ptr->select_jobinfo,
						     SELECT_DATA_START, 
						     &start);
				start_req = 1;
			}  else 
				error("BPs=%s is in a weird format", 
				      tmp_nodes); 
		} else {
			req_geometry[X] = (uint16_t)NO_VAL;
		}
		target_size = min_nodes;
	}
	
	/* this is where we should have the control flow depending on
	 * the spec arguement */
		
	*found_bg_record = NULL;
	allow = 0;
try_again:	
	slurm_mutex_lock(&block_state_mutex);
	debug("number of blocks to check: %d state %d", 
	      list_count(bg_list),
	      test_only);
	itr = list_iterator_create(bg_list);
	while ((record = (bg_record_t*) list_next(itr))) {		
		/* If test_only we want to fall through to tell the 
		   scheduler that it is runnable just not right now. 
		*/
		debug3("%s job_running = %d", 
		       record->bg_block_id, record->job_running);
		/*block is messed up some how (BLOCK_ERROR_STATE) ignore it*/
		if(record->job_running == BLOCK_ERROR_STATE) {
			debug("block %s is in an error state (can't use)", 
			      record->bg_block_id);			
			continue;
		} else if((record->job_running != NO_JOB_RUNNING) 
			  && !test_only) {
			debug("block %s in use by %s job %d", 
			      record->bg_block_id,
			      record->user_name,
			      record->job_running);
			continue;
		}
		
		/* Check processor count */
		proc_cnt = record->bp_count * record->cpus_per_bp;
		debug3("asking for %u-%u looking at %d", 
		      req_procs, max_procs, proc_cnt);
		if ((proc_cnt < req_procs)
		    || ((max_procs != NO_VAL) && (proc_cnt > max_procs))) {
			/* We use the proccessor count per block here
			   mostly to see if we can run on a smaller block. 
			 */
			convert_num_unit((float)proc_cnt, tmp_char, 
					 sizeof(tmp_char), UNIT_NONE);
			debug("block %s CPU count (%s) not suitable",
			      record->bg_block_id, 
			      tmp_char);
			continue;
		}

		/*
		 * check that the number of nodes is suitable
		 */
 		debug3("asking for %u-%u bps looking at %d", 
		      min_nodes, req_nodes, record->bp_count);
		if ((record->bp_count < min_nodes)
		    ||  (req_nodes != 0 && record->bp_count > req_nodes)
		    ||  (record->bp_count < target_size)) {
			convert_num_unit((float)record->node_cnt, tmp_char,
					 sizeof(tmp_char), UNIT_NONE);
			debug("block %s node count (%s) not suitable",
			      record->bg_block_id,
			      tmp_char);
			continue;
		}
		
		/*
		 * Next we check that this block's bitmap is within 
		 * the set of nodes which the job can use. 
		 * Nodes not available for the job could be down,
		 * drained, allocated to some other job, or in some 
		 * SLURM block not available to this job.
		 */
		if (!bit_super_set(record->bitmap, slurm_block_bitmap)) {
			debug("bg block %s has nodes not usable by this job",
			      record->bg_block_id);
			continue;
		}

		/*
		 * Insure that any required nodes are in this BG block
		 */
		if (job_ptr->details->req_node_bitmap
		    && (!bit_super_set(job_ptr->details->req_node_bitmap,
				       record->bitmap))) {
			debug("bg block %s lacks required nodes",
				record->bg_block_id);
			continue;
		}
				
		/* Make sure no other blocks are under this block 
		   are booted and running jobs
		*/
		itr2 = list_iterator_create(bg_list);
		while ((found_record = (bg_record_t*)
			list_next(itr2)) != NULL) {
			if ((!found_record->bg_block_id)
			    || (!strcmp(record->bg_block_id, 
					found_record->bg_block_id)))
				continue;
			if(blocks_overlap(record, found_record)) {
				if(!test_only
				   && bluegene_layout_mode == LAYOUT_OVERLAP) {
					if(!created && record->state 
						!= RM_PARTITION_READY)
						break;
					else if(created == 1 
						&& found_record->state 
						!= RM_PARTITION_FREE) {
						break;
					} 
				}
				if(!test_only && found_record->job_running
				   != NO_JOB_RUNNING) {
					if(found_record->job_running
					   == BLOCK_ERROR_STATE)
						error("can't use %s, "
						      "overlapping block %s "
						      "is in an error state.",
						      record->bg_block_id,
						      found_record->
						      bg_block_id);
					else
						debug("can't use %s, there is "
						      "a job (%d) running on "
						      "an overlapping "
						      "block %s", 
						      record->bg_block_id,
						      found_record->
						      job_running,
						      found_record->
						      bg_block_id);
				
					if(bluegene_layout_mode == 
					   LAYOUT_DYNAMIC) {
						list_remove(itr);
						temp_list = list_create(NULL);
						list_push(temp_list, record);
						num_block_to_free++;
						free_block_list(temp_list);
						list_destroy(temp_list);
					} 
					break;
				} 
			} 
		}
		list_iterator_destroy(itr2);

		if(found_record) {
			continue;
		} 

		if(check_image) {
			if(blrtsimage &&
			   strcasecmp(blrtsimage, record->blrtsimage)) {
				allow = 1;
				continue;
			}
			if(linuximage &&
			   strcasecmp(linuximage, record->linuximage)) {
				allow = 1;
				continue;
			}
			if(mloaderimage &&
			   strcasecmp(mloaderimage, record->mloaderimage)) {
				allow = 1;
				continue;
			}
			if(ramdiskimage &&
			   strcasecmp(ramdiskimage, record->ramdiskimage)) {
				allow = 1;
				continue;
			}			
		}
			
		/***********************************************/
		/* check the connection type specified matches */
		/***********************************************/
		if ((conn_type != record->conn_type)
		    && (conn_type != SELECT_NAV)) {
			debug("bg block %s conn-type not usable asking for %s "
			      "record is %s", 
			      record->bg_block_id,
			      convert_conn_type(conn_type),
			      convert_conn_type(record->conn_type));
			continue;
		} 

		/*****************************************/
		/* match up geometry as "best" possible  */
		/*****************************************/
		if (req_geometry[X] == (uint16_t)NO_VAL)
			;	/* Geometry not specified */
		else {	/* match requested geometry */
			bool match = false;
			rot_cnt = 0;	/* attempt six rotations  */
			
			for (rot_cnt=0; rot_cnt<6; rot_cnt++) {		
				if ((record->geo[X] >= req_geometry[X])
				&&  (record->geo[Y] >= req_geometry[Y])
				&&  (record->geo[Z] >= req_geometry[Z])) {
					match = true;
					break;
				}
				if (!rotate) {
					break;
				}
				_rotate_geo(req_geometry, rot_cnt);
			}

			if (!match) 
				continue;	/* Not usable */
		}
		*found_bg_record = record;
		debug2("we found one! %s", (*found_bg_record)->bg_block_id);
		break;
	}
	list_iterator_destroy(itr);

	/* set the bitmap and do other allocation activities */
	if (*found_bg_record) {
		if(!test_only) {
			if(check_block_bp_states(
				   (*found_bg_record)->bg_block_id) 
			   == SLURM_ERROR) {
				error("_find_best_block_match: Marking "
				      "block %s in an error state "
				      "because of bad bps.",
				      (*found_bg_record)->bg_block_id);
				(*found_bg_record)->job_running =
					BLOCK_ERROR_STATE;
				(*found_bg_record)->state = RM_PARTITION_ERROR;
				slurm_mutex_unlock(&block_state_mutex);
				trigger_block_error();
				goto try_again;
			}
		}
		format_node_name(*found_bg_record, tmp_char, sizeof(tmp_char));
	
		debug("_find_best_block_match %s <%s>", 
			(*found_bg_record)->bg_block_id, 
			tmp_char);
		bit_and(slurm_block_bitmap, (*found_bg_record)->bitmap);
		slurm_mutex_unlock(&block_state_mutex);
		rc = SLURM_SUCCESS;
		goto end_it;
	}

	/* see if we can just reset the image and reboot the block */
	if(allow) {
		check_image = 0;
		allow = 0;
		slurm_mutex_unlock(&block_state_mutex);
		goto try_again;
	}

	check_image = 1;
	/* all these assume that the *found_bg_record is NULL */
	if(bluegene_layout_mode == LAYOUT_OVERLAP && !test_only && created<2) {
		created++;
		slurm_mutex_unlock(&block_state_mutex);
		goto try_again;
	}
		
	slurm_mutex_unlock(&block_state_mutex);
	if(bluegene_layout_mode != LAYOUT_DYNAMIC)
		goto not_dynamic;
	
	if(test_only) {
		for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
			request.start[i] = start[i];
			
		for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
			request.geometry[i] = req_geometry[i];
			
		request.save_name = NULL;
		request.elongate_geos = NULL;
		request.size = target_size;
		request.procs = req_procs;
		request.conn_type = conn_type;
		request.rotate = rotate;
		request.elongate = true;
		request.start_req = start_req;
		request.blrtsimage = blrtsimage;
		request.linuximage = linuximage;
		request.mloaderimage = mloaderimage;
		request.ramdiskimage = ramdiskimage;
		if(job_ptr->details->req_node_bitmap) 
			request.avail_node_bitmap = 
				job_ptr->details->req_node_bitmap;
		else
			request.avail_node_bitmap = slurm_block_bitmap;

		debug("trying with all free blocks");
		if(create_dynamic_block(&request, NULL) == SLURM_ERROR) {
			error("this job will never run on "
			      "this system");
			xfree(request.save_name);
			rc = SLURM_ERROR;
			goto end_it;
		} else {
			if(!request.save_name) {
				error("no name returned from "
				      "create_dynamic_block");
				rc = SLURM_ERROR;
				goto end_it;
			} 

			slurm_conf_lock();
			snprintf(tmp_char, sizeof(tmp_char), "%s%s", 
				 slurmctld_conf.node_prefix,
				 request.save_name);
			slurm_conf_unlock();
			if (node_name2bitmap(tmp_char, 
					     false, 
					     &tmp_bitmap)) {
				fatal("Unable to convert nodes %s to bitmap", 
				      tmp_char);
			}
			
			bit_and(slurm_block_bitmap, tmp_bitmap);
			FREE_NULL_BITMAP(tmp_bitmap);
			xfree(request.save_name);
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	} else if(!created) {
		debug2("going to create %d", target_size);
		lists_of_lists = list_create(NULL);
		if(job_ptr->details->req_nodes) {
			list_append(lists_of_lists, bg_job_block_list);
		} else {
			list_append(lists_of_lists, bg_list);
			if(list_count(bg_list)
			   != list_count(bg_booted_block_list)) {
				list_append(lists_of_lists, 
					    bg_booted_block_list);
				if(list_count(bg_booted_block_list) 
				   != list_count(bg_job_block_list)) 
					list_append(lists_of_lists, 
						    bg_job_block_list);
			} else if(list_count(bg_list) 
				  != list_count(bg_job_block_list)) 
				list_append(lists_of_lists, bg_job_block_list);
		}
		itr = list_iterator_create(lists_of_lists);
		while ((temp_list = (List)list_next(itr))) {
			created++;

			for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
				request.start[i] = start[i];
			
			for(i=0; i<BA_SYSTEM_DIMENSIONS; i++) 
				request.geometry[i] = req_geometry[i];
			
			request.save_name = NULL;
			request.elongate_geos = NULL;
			request.size = target_size;
			request.procs = req_procs;
			request.conn_type = conn_type;
			request.rotate = rotate;
			request.elongate = true;
			request.start_req = start_req;
			request.blrtsimage = blrtsimage;
			request.linuximage = linuximage;
			request.mloaderimage = mloaderimage;
			request.ramdiskimage = ramdiskimage;
			if(job_ptr->details->req_node_bitmap) 
				request.avail_node_bitmap = 
					job_ptr->details->req_node_bitmap;
			else
				request.avail_node_bitmap = slurm_block_bitmap;
			
			/* 1- try empty space
			   2- we see if we can create one in the 
			   unused bps
			   3- see if we can create one in the non 
			   job running bps
			*/
			debug("trying with %d", created);
			if(create_dynamic_block(&request, temp_list) 
			   == SLURM_SUCCESS) {
				list_iterator_destroy(itr);
				list_destroy(lists_of_lists);
				lists_of_lists = NULL;
				goto try_again;
			}
		}
		list_iterator_destroy(itr);
		if(lists_of_lists)
			list_destroy(lists_of_lists);
	}
not_dynamic:
	debug("_find_best_block_match none found");
	rc = SLURM_ERROR;

end_it:
	xfree(blrtsimage);
	xfree(linuximage);
	xfree(mloaderimage);
	xfree(ramdiskimage);
		
	return rc;
}

/*
 * Try to find resources for a given job request
 * IN job_ptr - pointer to job record in slurmctld
 * IN/OUT bitmap - nodes availble for assignment to job, clear those not to
 *	be used
 * IN min_nodes, max_nodes  - minimum and maximum number of nodes to allocate
 *	to this job (considers slurm block limits)
 * IN test_only - if true, only test if ever could run, not necessarily now
 * RET - SLURM_SUCCESS if job runnable now, error code otherwise
 */
extern int submit_job(struct job_record *job_ptr, bitstr_t *slurm_block_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes, uint32_t req_nodes, 
		      bool test_only)
{
	int spec = 1; /* this will be like, keep TYPE a priority, etc,  */
	bg_record_t* record = NULL;
	char buf[100];
	int i, rc = SLURM_SUCCESS;
	uint16_t geo[BA_SYSTEM_DIMENSIONS];
	uint16_t tmp16 = (uint16_t)NO_VAL;
	
	
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MIXED);
	debug("bluegene:submit_job: %s nodes=%u-%u-%u", 
	      buf, min_nodes, req_nodes, max_nodes);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_BLRTS_IMAGE);
	debug2("BlrtsImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_LINUX_IMAGE);
	debug2("LinuxImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_MLOADER_IMAGE);
	debug2("MloaderImage=%s", buf);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo, buf, sizeof(buf), 
				SELECT_PRINT_RAMDISK_IMAGE);
	debug2("RamDiskImage=%s", buf);
	
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_lock(&create_dynamic_mutex);
	
	rc = _find_best_block_match(job_ptr, slurm_block_bitmap, min_nodes, 
				    max_nodes, req_nodes, spec, 
				    &record, test_only);
	
	if(rc == SLURM_SUCCESS) {
		if(!record) {
			debug2("can run, but block not made");
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID,
					     "unassigned");
			if(job_ptr->num_procs < bluegene_bp_node_cnt 
				&& job_ptr->num_procs > 0) {
				i = procs_per_node/job_ptr->num_procs;
				debug2("divide by %d", i);
			} else 
				i = 1;
			min_nodes *= bluegene_bp_node_cnt/i;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODE_CNT,
					     &min_nodes);
			
			for(i=0; i<BA_SYSTEM_DIMENSIONS; i++)
				geo[i] = 0;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_GEOMETRY, 
					     &geo);
			
		} else {
			slurm_mutex_lock(&block_state_mutex);

			if((record->ionodes)
			   && (job_ptr->part_ptr->shared == 0))
				error("Small block used in "
				      "non-shared partition");

			/* set the block id and info about block */
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID, 
					     record->bg_block_id);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_IONODES, 
					     record->ionodes);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_NODE_CNT, 
					     &record->node_cnt);
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_GEOMETRY, 
					     &record->geo);
			tmp16 = record->conn_type;
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_CONN_TYPE, 
					     &tmp16);
			slurm_mutex_unlock(&block_state_mutex);
		}
		if(test_only) {
			select_g_set_jobinfo(job_ptr->select_jobinfo,
					     SELECT_DATA_BLOCK_ID,
					     "unassigned");
		} 
	}
	if(bluegene_layout_mode == LAYOUT_DYNAMIC)
		slurm_mutex_unlock(&create_dynamic_mutex);
	
	return rc;
}