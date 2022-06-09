/*****************************************************************************\
 *  config.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021-2022 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include "switch_hpe_slingshot.h"

/* Set this to true if VNI table is re-sized and loses some bits */
static bool lost_vnis = false;
/* Number of free VNIs */
static int free_vnis = 0;

/*
 * Set up slingshot_config defaults
 */
static void _config_defaults(void)
{
	memset(&slingshot_config, 0, sizeof(slingshot_config_t));

	slingshot_config.single_node_vni = false;
	slingshot_config.job_vni = SLINGSHOT_JOB_VNI_NONE;

	slingshot_config.limits.txqs.max = SLINGSHOT_TXQ_MAX;
	slingshot_config.limits.tgqs.max = SLINGSHOT_TGQ_MAX;
	slingshot_config.limits.eqs.max = SLINGSHOT_EQ_MAX;
	slingshot_config.limits.cts.max = SLINGSHOT_CT_MAX;
	slingshot_config.limits.tles.max = SLINGSHOT_TLE_MAX;
	slingshot_config.limits.ptes.max = SLINGSHOT_PTE_MAX;
	slingshot_config.limits.les.max = SLINGSHOT_LE_MAX;
	slingshot_config.limits.acs.max = SLINGSHOT_AC_MAX;

	slingshot_config.limits.txqs.def = SLINGSHOT_TXQ_DEF;
	slingshot_config.limits.tgqs.def = SLINGSHOT_TGQ_DEF;
	slingshot_config.limits.eqs.def = SLINGSHOT_EQ_DEF;
	slingshot_config.limits.cts.def = SLINGSHOT_CT_DEF;
	slingshot_config.limits.tles.def = SLINGSHOT_TLE_DEF;
	slingshot_config.limits.ptes.def = SLINGSHOT_PTE_DEF;
	slingshot_config.limits.les.def = SLINGSHOT_LE_DEF;
	slingshot_config.limits.acs.def = SLINGSHOT_AC_DEF;
}

/*
 * Parse the VNI min/max token, with format "vni=<min>-<max>";
 * put results in *minp, *maxp
 */
static bool _config_vnis(const char *token, uint16_t *min_ptr,
			 uint16_t *max_ptr)
{
	char *arg, *end_ptr;
	int min, max;

	if (!(arg = strchr(token, '=')))
		goto error;
	arg++;
	end_ptr = NULL;
	min = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '-'))
		goto error;
	if ((min < SLINGSHOT_VNI_MIN) || (min > SLINGSHOT_VNI_MAX))
		goto error;

	arg = end_ptr + 1;
	end_ptr = NULL;
	max = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '\0'))
		goto error;
	if ((max <= min) || (max > SLINGSHOT_VNI_MAX))
		goto error;

	*min_ptr = min;
	*max_ptr = max;
	log_flag(SWITCH, "[token=%s]: min/max %hu %hu", token, min, max);
	return true;

error:
	error("Invalid vni token '%s' (example: 'vnis=10-100', valid range %d-%d)",
	      token, SLINGSHOT_VNI_MIN, SLINGSHOT_VNI_MAX);
	return false;
}

/*
 * Compare old slingshot_state.vni_{min,max} with passed-in min/max;
 * if old table is incompatible with new min/max, return false;
 * otherwise set up slingshot_state with new vni_table values
 */
static bool _setup_vni_table(uint16_t min, uint16_t max)
{
	int32_t oldbits, newbits;
	size_t oldsize = slingshot_state.vni_max - slingshot_state.vni_min + 1;
	size_t newsize = max - min + 1;
	uint16_t oldmin = slingshot_state.vni_min;
	uint16_t oldmax = slingshot_state.vni_max;
	bitstr_t *table = slingshot_state.vni_table;

	log_flag(SWITCH, "oldmin/max/size %hu %hu %zu min/max/size %hu %hu %zu",
		 oldmin, oldmax, oldsize, min, max, newsize);

	/* If no recovery of vni_table, just set up new one */
	if (!slingshot_state.vni_table) {
		table = bit_alloc(newsize);
		newbits = 0;	/* no VNIs used */
		goto done;
	}

	xassert(oldmin);
	xassert(oldmax);
	xassert(oldsize > 0);
	xassert(newsize > 0);
	xassert(table);
	xassert(bit_size(table) == oldsize);

	if ((oldmin == min) && (oldmax == max)) {
		newbits = bit_set_count(table);
		goto done;
	}

	/* Re-size bitstring if needed */
	oldbits = bit_set_count(table);
	if (oldsize != newsize)
		table = bit_realloc(table, newsize);

	/* Shift bits if vni_min is changing */
	if (oldmin != min)
		bit_rotate(table, min - oldmin);

	newbits = bit_set_count(table);
	/* Go on even if we're losing VNIs */
	if (newbits != oldbits) {
		error("WARNING: changing vni_min/max %hu %hu -> %hu %hu; %d VNIs will be lost!",
		      oldmin, oldmax, min, max, oldbits - newbits);
		lost_vnis = true;
	}

done:
	free_vnis = newsize - newbits;
	slingshot_state.vni_min = min;
	slingshot_state.vni_max = max;
	if ((slingshot_state.vni_last < min) ||
	    (slingshot_state.vni_last >= max))
		slingshot_state.vni_last = min - 1;
	slingshot_state.vni_table = table;

	log_flag(SWITCH, "version=%d min/max/last=%hu %hu %hu num_vnis=%zu used=%d free_vnis=%d",
		 slingshot_state.version, slingshot_state.vni_min,
		 slingshot_state.vni_max, slingshot_state.vni_last, newsize,
		 newbits, free_vnis);
	return true;
}

/* Mapping between Slingshot traffic class labels and their bitmasks */
static struct {
	const char *label;
	uint32_t bit;
} classes[] = {
	{ "DEDICATED_ACCESS", SLINGSHOT_TC_DEDICATED_ACCESS },
	{ "LOW_LATENCY", SLINGSHOT_TC_LOW_LATENCY },
	{ "BULK_DATA", SLINGSHOT_TC_BULK_DATA },
	{ "BEST_EFFORT", SLINGSHOT_TC_BEST_EFFORT },
};
const int num_classes = sizeof(classes) / sizeof(classes[0]);

/*
 * Parse the Slingshot traffic classes token, with format
 * "tcs=<class1>:<class2>[:...]
 */
static bool _config_tcs(const char *token)
{
	char *arg, *save_ptr = NULL, *tcs = NULL, *tc;
	uint32_t tcbits = 0;
	int i;

	if (!(arg = strchr(token, '=')))
		goto err;
	arg++;
	tcs = xstrdup(arg);
	for (tc = strtok_r(tcs, ":", &save_ptr); tc;
		tc = strtok_r(NULL, ":", &save_ptr)) {
		for (i = 0; i < num_classes; i++) {
			if (!xstrcasecmp(tc, classes[i].label)) {
				tcbits |= classes[i].bit;
				break;
			}
		}
		if (i == num_classes)
			goto err;
	}
	if (tcbits == 0)
		goto err;

	slingshot_config.tcs = tcbits;
	log_flag(SWITCH, "[token=%s]: tcs %#x", token, tcbits);
	xfree(tcs);
	return true;

err:
	xfree(tcs);
	error("Invalid traffic class token '%s' (example 'tcs=DEDICATED_ACCESS:LOW_LATENCY:BULK_DATA:BEST_EFFORT')",
	      token);
	return false;
}

/*
 * Parse the Slingshot job VNI token, with format "job_vni={all,user,none}"
 */
static bool _config_job_vni(const char *token)
{
	char *arg;

	/* Backwards compatibility: no argument = SLINGSHOT_JOB_VNI_ALL */
	if (!(arg = strchr(token, '='))) {
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_ALL;
		goto out;
	}
	arg++;
	if (!xstrcasecmp(arg, "all"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_ALL;
	else if (!xstrcasecmp(arg, "user"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_USER;
	else if (!xstrcasecmp(arg, "none"))
		slingshot_config.job_vni = SLINGSHOT_JOB_VNI_NONE;
	else {
		error("Invalid job_vni token '%s' (example 'job_vni={all,user,none}')",
		      token);
		return false;
	}

out:
	log_flag(SWITCH, "[token=%s]: job_vni %d",
		 token, slingshot_config.job_vni);
	return true;
}

/*
 * Mapping between Slingshot limit names, slingshot_limits_set_t offset, maximum
 * values
 */
typedef struct limits_table {
	const char *name;
	size_t offset;
	int max;
} limits_table_t;
static limits_table_t limits_table[] = {
	{ "txqs", offsetof(slingshot_limits_set_t, txqs), SLINGSHOT_TXQ_MAX },
	{ "tgqs", offsetof(slingshot_limits_set_t, tgqs), SLINGSHOT_TGQ_MAX },
	{ "eqs",  offsetof(slingshot_limits_set_t, eqs),  SLINGSHOT_EQ_MAX },
	{ "cts",  offsetof(slingshot_limits_set_t, cts),  SLINGSHOT_CT_MAX },
	{ "tles", offsetof(slingshot_limits_set_t, tles), SLINGSHOT_TLE_MAX },
	{ "ptes", offsetof(slingshot_limits_set_t, ptes), SLINGSHOT_PTE_MAX },
	{ "les",  offsetof(slingshot_limits_set_t, les),  SLINGSHOT_LE_MAX },
	{ "acs",  offsetof(slingshot_limits_set_t, acs),  SLINGSHOT_AC_MAX },
};
static const int num_limits = sizeof(limits_table) / sizeof(limits_table[0]);
static const char *all_limits = "txqs,tgqs,eqs,cts,tles,ptes,les,acs";

/*
 * Check whether the token is a Slingshot resource limit token,
 * with format "{def,res,max}_{name}=<limit>"; update slingshot_config
 */
static bool _config_limits(const char *token, slingshot_limits_set_t *limits)
{
	char *tok, *arg, *end_ptr;
	const char *name, *typestr;
	enum { DEF = 1, RES, MAX } type;
	int i, limit;
	const char def_str[] = "def_";
	const size_t def_siz = sizeof(def_str) - 1;
	const char res_str[] = "res_";
	const size_t res_siz = sizeof(res_str) - 1;
	const char max_str[] = "max_";
	const size_t max_siz = sizeof(max_str) - 1;
	limits_table_t *entry;
	slingshot_limits_t *limit_ptr;

	tok = xstrdup(token);
	if (!(arg = strchr(tok, '=')))
		goto err;
	*arg = '\0';	/* null-terminate limit name */
	arg++;
	/* Parse "{def,res,max}_" prefix */
	if (!xstrncmp(tok, def_str, def_siz)) {
		type = DEF;
		typestr = "def";
		name = tok + def_siz;
	} else if (!xstrncmp(tok, res_str, res_siz)) {
		type = RES;
		typestr = "res";
		name = tok + res_siz;
	} else if (!xstrncmp(tok, max_str, max_siz)) {
		type = MAX;
		typestr = "max";
		name = tok + max_siz;
	} else {
		goto err;
	}
	/* Now find the limit type and point entry at the limit_table slot */
	entry = NULL;
	for (i = 0; i < num_limits; i++) {
		if (!xstrcmp(name, limits_table[i].name)) {
			entry = &limits_table[i];
			break;
		}
	}
	if (!entry)
		goto err;
	end_ptr = NULL;
	limit = strtol(arg, &end_ptr, 10);
	if (!end_ptr || (end_ptr == arg) || (*end_ptr != '\0'))
		goto err;
	if ((limit < 0) || (limit > entry->max)) {
		error("Invalid limit token '%s': invalid limit %d (valid range 0-%d)",
		      token, limit, entry->max);
		goto out;
	}
	limit_ptr = (slingshot_limits_t *)(((void *) limits) + entry->offset);
	if (type == DEF) {
		limit_ptr->def = limit;
	} else if (type == RES) {
		limit_ptr->res = limit;
	} else if (type == MAX) {
		limit_ptr->max = limit;
	}
	log_flag(SWITCH, "[token=%s]: limits[%d].%s.%s %d",
		 token, i, entry->name, typestr, limit);
	xfree(tok);
	return true;
err:
	error("Invalid limit token '%s' (example {max,res,def}_{%s})",
	      token, all_limits);
out:
	xfree(tok);
	return false;
}

static void _print_limits(slingshot_limits_set_t *limits)
{
#define DEBUG_LIMIT(SET, LIM) \
	debug("%s: max/res/def %hu %hu %hu", \
		#LIM, SET->LIM.max, SET->LIM.res, SET->LIM.def);
	DEBUG_LIMIT(limits, txqs);
	DEBUG_LIMIT(limits, tgqs);
	DEBUG_LIMIT(limits, eqs);
	DEBUG_LIMIT(limits, cts);
	DEBUG_LIMIT(limits, tles);
	DEBUG_LIMIT(limits, ptes);
	DEBUG_LIMIT(limits, les);
	DEBUG_LIMIT(limits, acs);
#undef DEBUG_LIMIT
}

/*
 * Set up passed-in slingshot_config_t based on values in 'SwitchParameters'
 * slurm.conf setting.  Return true on success, false on bad parameters
 */
extern bool slingshot_setup_config(const char *switch_params)
{
	bool vni_set = false;
	char *params = NULL, *token, *save_ptr = NULL;
	const char vnis[] = "vnis";
	const size_t size_vnis = sizeof(vnis) - 1;
	const char tcs[] = "tcs";
	const size_t size_tcs = sizeof(tcs) - 1;
	const char job_vni[] = "job_vni";
	const size_t size_job_vni = sizeof(job_vni) - 1;

	log_flag(SWITCH, "switch_params=%s", switch_params);
	/*
	 * Handle SwitchParameters values (separated by commas):
	 *
	 *   vnis=<start>-<end> (e.g. vnis=1-16000)
	 *   tcs=<tc_list> (e.g. tcs=BULK_DATA:BEST_EFFORT)
	 *   single_node_vni: allocate VNI for single-node steps
	 *   job_vni=<all,user>: allocate additional VNI per-job for all jobs,
	 *     or only on user request (srun --network=job_vni)
	 *   def_<NIC_resource>: default per-thread value for resource
	 *   res_<NIC_resource>: reserved value for resource
	 *   max_<NIC_resource>: maximum value for resource
	 *
	 * NIC resources are:
	 *   txqs: transmit command queues
	 *   tgqs: target command queues
	 *   eqs:  events queues
	 *   cts:  counters
	 *   tles: trigger list entries
	 *   ptes: portable table entries
	 *   les:  list entries
	 *   acs:  addressing contexts
	 */

	_config_defaults();
	if (!switch_params)
		goto out;

	params = xstrdup(switch_params);
	for (token = strtok_r(params, ",", &save_ptr); token;
		token = strtok_r(NULL, ",", &save_ptr)) {
		if (!xstrncasecmp(token, vnis, size_vnis)) {
			uint16_t min, max;
			if (!_config_vnis(token, &min, &max))
				goto err;
			/* See if any incompatible changes in VNI range */
			if (!_setup_vni_table(min, max))
				goto err;
			vni_set = true;
		} else if (!xstrncasecmp(token, tcs, size_tcs)) {
			if (!_config_tcs(token))
				goto err;
		} else if (!xstrncasecmp(token, job_vni, size_job_vni)) {
			if (!_config_job_vni(token))
				goto err;
		} else if (!xstrcasecmp(token, "single_node_vni")) {
			slingshot_config.single_node_vni = true;
		} else {
			if (!_config_limits(token, &slingshot_config.limits))
				goto err;
		}
	}

out:
	if (!vni_set)
		fatal("Need at least SwitchParameters=vnis=... set");

	debug("single_node_vni=%d job_vni=%d tcs=%#x",
	      slingshot_config.single_node_vni, slingshot_config.job_vni,
	      slingshot_config.tcs);
	_print_limits(&slingshot_config.limits);

	xfree(params);
	return true;

err:
	xfree(params);
	return false;
}

/*
 * Allocate a free VNI (range vni_min... vni_max, starting at vni_last + 1)
 * Return true with *vnip filled in on success, false on failure
 */
static bool _alloc_vni(uint16_t *vnip)
{
	bitoff_t start, end, bit;
	uint16_t vni;

	/* Search for clear bit from [vni_last + 1...vni_max] */
	start = (slingshot_state.vni_last + 1) - slingshot_state.vni_min;
	end = slingshot_state.vni_max - slingshot_state.vni_min;
	xassert(start >= 0);
	log_flag(SWITCH, "upper bits: start/end %zu %zu", start, end);
	for (bit = start; bit <= end; bit++) {
		if (!bit_test(slingshot_state.vni_table, bit))
			goto gotvni;
	}
	/* Search for clear bit from [vni_min...vni_last] */
	start = 0;
	end = slingshot_state.vni_last - slingshot_state.vni_min;
	log_flag(SWITCH, "lower bits: start/end %zu %zu", start, end);
	for (bit = start; bit <= end; bit++) {
		if (!bit_test(slingshot_state.vni_table, bit))
			goto gotvni;
	}
	/* TODO: developer's mode: check for no bits set? */
	error("Cannot allocate VNI (min/max/last %hu %hu %hu)",
	      slingshot_state.vni_min, slingshot_state.vni_max,
	      slingshot_state.vni_last);
	return false;

gotvni:
	bit_set(slingshot_state.vni_table, bit);
	xassert(bit + slingshot_state.vni_min <= SLINGSHOT_VNI_MAX);
	vni = bit + slingshot_state.vni_min;
	slingshot_state.vni_last = vni;
	free_vnis--;
	xassert(free_vnis >= 0);
	log_flag(SWITCH, "min/max/last %hu %hu %hu vni=%hu free_vnis=%d",
		 slingshot_state.vni_min, slingshot_state.vni_max,
		 slingshot_state.vni_last, vni, free_vnis);
	*vnip = vni;
	return true;
}

/*
 * Allocate a per-job inter-job-step VNI.
 * If this is the first allocation for this job ID,
 * allocate a new VNI and add it to the job_vnis table;
 * otherwise return the VNI from the table for this job ID
 * Return true with *vnip filled in on success, false on failure
 */
static bool _alloc_job_vni(uint32_t job_id, uint16_t *vnip)
{
	int i, freeslot = -1;
	job_vni_t *jobvni;

	/*
	 * Check if this jobID is in the table already:
	 * if so, fill in *vnip and return true;
	 * otherwise, save the first free index into the table.
	 */
	for (i = 0; i < slingshot_state.num_job_vnis; i++) {
		jobvni = &slingshot_state.job_vnis[i];
		if (jobvni->job_id == job_id) {
			log_flag(SWITCH,
				 "[job_id=%u]: found job_vnis[%d] vni=%hu num_job_vnis=%d",
				 job_id, i, jobvni->vni,
				 slingshot_state.num_job_vnis);
			*vnip = jobvni->vni;
			return true;
		} else if (jobvni->job_id == 0) {
			freeslot = i;
		}
	}

	/* If no free slot, allocate a new slot in the job_vnis table */
	if (freeslot < 0) {
		freeslot = slingshot_state.num_job_vnis;
		slingshot_state.num_job_vnis++;
		xrecalloc(slingshot_state.job_vnis,
			slingshot_state.num_job_vnis, sizeof(job_vni_t));
	}

	if (!_alloc_vni(vnip))
		return false;

	slingshot_state.job_vnis[freeslot].job_id = job_id;
	slingshot_state.job_vnis[freeslot].vni = *vnip;
	log_flag(SWITCH, "[job_id=%u]: new vni[%d] vni=%hu num_job_vnis=%d",
		 job_id, freeslot, *vnip, slingshot_state.num_job_vnis);
	return true;
}

/*
 * Free an allocated VNI
 */
static void _free_vni(uint16_t vni)
{
	/*
	 * Range-check VNI, but only if table has been re-sized and VNIs
	 * were lost
	 */
	if (lost_vnis &&
	    ((vni < slingshot_state.vni_min) ||
	     (vni > slingshot_state.vni_max))) {
		info("vni %hu: not in current table min/max %hu-%hu",
			vni, slingshot_state.vni_min, slingshot_state.vni_max);
		return;
	}
	bitoff_t bit = vni - slingshot_state.vni_min;
	if (!bit_test(slingshot_state.vni_table, bit)) {
		error("WARNING: _free_vni(%hu): bit %zu not set in vni_table!",
		      vni, bit);
		return;
	}
	bit_clear(slingshot_state.vni_table, bit);
	free_vnis++;
	xassert(free_vnis <=
		(slingshot_state.vni_max - slingshot_state.vni_min) + 1);
	log_flag(SWITCH, "[vni=%hu]: bit %zu", vni, bit);
}

/*
 * Free an allocated per-job "user" VNI
 * Return the VNI, or 0 if not found
 */
static uint16_t _free_job_vni(uint32_t job_id)
{
	int i;
	job_vni_t *jobvni = NULL;

	/*
	 * Find the jobID/vni in the job_vnis table;
	 * zero-out the slot in the table
	 */
	for (i = 0; i < slingshot_state.num_job_vnis; i++) {
		jobvni = &slingshot_state.job_vnis[i];
		if (jobvni->job_id == job_id) {
			uint16_t vni = jobvni->vni;
			_free_vni(vni);
			log_flag(SWITCH,
				 "[job_id=%u]: free job_vnis[%d] vni=%hu num_job_vnis=%d free_vnis=%d",
				 jobvni->job_id, i, vni,
				 slingshot_state.num_job_vnis, free_vnis);
			memset(jobvni, 0, sizeof(*jobvni));
			return vni;
		}
	}
	if (slingshot_state.num_job_vnis > 0)
		error("job_id=%u: not found in job_vnis[%d]",
		      job_id, slingshot_state.num_job_vnis);
	return 0;
}

/*
 * Parse --network 'depth=<value>' token: return value, or 0 on error
 */
static uint32_t _setup_depth(const char *token)
{
	uint32_t ret;
	char *arg = strchr(token, '=');
	if (!arg)
		goto err;
	arg++;	/* point to argument */
	char *end_ptr = NULL;
	ret = strtol(arg, &end_ptr, 10);
	if (*end_ptr || ret < 1 || ret > 1024)
		goto err;
	log_flag(SWITCH, "[token=%s]: depth %u", token, ret);
	return ret;
err:
	error("Invalid depth token '%s' (valid range %d-%d)", token, 1, 1024);
	return 0;
}

/*
 * Set up passed-in slingshot_job_t based on values in srun --network
 * parameters.  Return true on successful parsing, false otherwise.
 */
static bool _setup_network_params(const char *network_params,
				  slingshot_jobinfo_t *job,
				  bool *job_vni)
{
	char *params = NULL, *token, *save_ptr = NULL;

	log_flag(SWITCH, "network_params=%s", network_params);

	/* First, copy limits from slingshot_config to job */
	job->limits = slingshot_config.limits;

	/* Then get configured job VNI setting */
	if (slingshot_config.job_vni == SLINGSHOT_JOB_VNI_ALL)
		*job_vni = true;
	else
		*job_vni = false;

	/*
	 * Handle srun --network argument values (separated by commas):
	 *
	 *   depth: value to be used for threads-per-rank
	 *   job_vni: allocate a job VNI for this job
	 *   def_<NIC_resource>: default per-thread value for resource
	 *   res_<NIC_resource>: reserved value for resource
	 *   max_<NIC_resource>: maximum value for resource
	 */
	if (!network_params)
		return true;

	params = xstrdup(network_params);
	char depth_str[] = "depth";
	size_t depth_siz = sizeof(depth_str) - 1;
	char job_vni_str[] = "job_vni";
	size_t job_vni_siz = sizeof(job_vni_str) - 1;
	for (token = strtok_r(params, ",", &save_ptr); token;
		token = strtok_r(NULL, ",", &save_ptr)) {
		if (!xstrncmp(token, depth_str, depth_siz)) {
			if ((job->depth = _setup_depth(token)) == 0)
				goto err;
		} else if (!xstrncmp(token, job_vni_str, job_vni_siz)) {
			if (token[job_vni_siz] != '\0') {
				error("Invalid job VNI token '%s'", token);
				goto err;
			} else if (slingshot_config.job_vni ==
				   SLINGSHOT_JOB_VNI_NONE) {
				error("Job VNI requested by user, but 'job_vni=<all|user>' not set in SwitchParameters");
				goto err;
			} else
				*job_vni = true;
		} else if (!_config_limits(token, &job->limits))
			goto err;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)
		_print_limits(&job->limits);
	xfree(params);
	return true;
err:
	xfree(params);
	return false;
}

/*
 * Set up slingshot_jobinfo_t struct with VNIs, and CXI limits,
 * based on configured limits as well as any specified with
 * the --network option
 * Return true on success, false if VNI cannot be allocated,
 * or --network parameters have syntax errors
 */
extern bool slingshot_setup_job_step(slingshot_jobinfo_t *job, int node_cnt,
				     uint32_t job_id,
				     const char *network_params)
{
	int alloc_vnis = 0;
	uint16_t vni = 0, job_vni = 0;
	bool alloc_job_vni;

	/*
	 * If --network specified, add any depth/limits/job_vni settings
	 * Copy configured Slingshot limits to job, add any --network settings
	 */
	job->limits = slingshot_config.limits;
	if (!_setup_network_params(network_params, job, &alloc_job_vni))
		goto err;

	/*
	 * VNIs and traffic classes are not allocated for single-node jobs,
	 * unless 'single_node_vni' is set in the configuration
	 */
	job->num_vnis = 0;
	if ((node_cnt > 1) || slingshot_config.single_node_vni) {
		alloc_vnis++;
		job->tcs = slingshot_config.tcs;
	}
	if (alloc_job_vni)
		alloc_vnis++;

	job->vnis = xcalloc(alloc_vnis, sizeof(uint16_t));
	if (alloc_vnis >= 1) {
		if (!_alloc_vni(&job->vnis[0]))
			goto err;
		vni = job->vnis[0];
		job->num_vnis++;
	}
	/* Allocate (first step in job) or get job VNI */
	if (alloc_vnis == 2) {
		if (!_alloc_job_vni(job_id, &job->vnis[1]))
			goto err;
		job_vni = job->vnis[1];
		job->num_vnis++;
	}
	debug("allocate vni=%hu job_vni=%hu free_vnis=%d",
	      vni, job_vni, free_vnis);

	/* profiles are allocated in slurmd */
	job->num_profiles = 0;
	job->profiles = NULL;

	return true;

err:
	if (job->vnis) {
		if (job->num_vnis > 0)
			_free_vni(job->vnis[0]);
		if (job->num_vnis > 1)
			(void)_free_job_vni(job_id);
		xfree(job->vnis);
	}

	return false;
}

/*
 * Free job-step VNI (if any)
 */
extern void slingshot_free_job_step(slingshot_jobinfo_t *job)
{
	/* Second VNI is a job VNI - don't free until job is complete */
	if (job->vnis && (job->num_vnis > 0)) {
		_free_vni(job->vnis[0]);
		debug("free vni=%hu free_vnis=%d", job->vnis[0], free_vnis);
	}
}

/*
 * Free this job's job-specific VNI; called at end of job
 */
extern void slingshot_free_job(uint32_t job_id)
{
	uint16_t vni = _free_job_vni(job_id);
	debug("free job_vni=%hu free_vnis=%d", vni, free_vnis);
}
