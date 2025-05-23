/*****************************************************************************\
 *  sack_api.h - [S]lurm's [a]uth and [c]red [k]iosk API
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _SACK_API_H
#define _SACK_API_H

/*
 * The 64000 range is reserved for SACK to
 * avoid collisions with slurm_msg_type_t.
 */
typedef enum {
	SACK_CREATE = 64001,
	SACK_VERIFY = 64002,
} sack_msg_t;

#define SACK_HEADER_LENGTH (sizeof(uint16_t) + 2 * sizeof(uint32_t))

/*
 * Create a SACK token with an (optional) binary payload.
 *
 * The r_uid restricts decoding to a specific uid,
 * or SLURM_AUTH_UID_ANY to permit anyone to decode.
 *
 * Optional cluster_name for multi-sackd UNIX socket path support or NULL.
 *
 * Returns an xmalloc()'d string, caller must xfree().
 */
extern char *sack_create(uid_t r_uid, void *data, int dlen, char *cluster_name);

/*
 * Given a SACK token, ensure the signature is valid,
 * that the calling process has permissions to decode,
 * and that it has not expired.
 *
 * Only works for "auth" and "sack" context tokens,
 * all others will be rejected.
 *
 * Optional cluster_name for multi-sackd UNIX socket path support or NULL.
 *
 * Returns SLURM_SUCCESS if verified.
 */
extern int sack_verify(char *token, char *cluster_name);

#endif
