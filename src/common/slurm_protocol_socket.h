/*****************************************************************************\
 *  slurm_protocol_socket.h - mid-level slurm communication definitions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SLURM_PROTOCOL_SOCKET_H
#define _SLURM_PROTOCOL_SOCKET_H

#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"

/*****************/
/* msg functions */
/*****************/

/* slurm_msg_recvfrom_timeout reads len bytes from file descriptor fd
 * timing out after `timeout' milliseconds.
 *
 */
extern ssize_t slurm_msg_recvfrom_timeout(void *tls_conn, char **pbuf,
					  size_t *lenp, int timeout);

/* slurm_msg_sendto
 * Send message over the given connection, default timeout value
 * IN open_fd - an open file descriptor
 * IN tls_conn - an open TLS connection (NULL if no TLS connection)
 * IN buffer - data to transmit
 * IN size - size of buffer in bytes
 * RET number of bytes written
 */
extern ssize_t slurm_msg_sendto(void *tls_conn, char *buffer, size_t size);
extern ssize_t slurm_msg_sendto_socket(int fd, char *buffer, size_t size);

/*
 * Send message over the given connection, default timeout value
 * IN tls_conn - tls connection context
 * IN buffers - array of buffers to transmit
 * RET number of bytes written or SLURM_ERROR on error
 */
extern ssize_t slurm_bufs_sendto(void *tls_conn, msg_bufs_t *buffers);

/********************/
/* stream functions */
/********************/

/* slurm_init_msg_engine
 * opens a stream server and listens on it
 * IN slurm_address 	- slurm_addr_t to bind the server stream to
 * IN quiet		- log failure errors at "debug" level instead
 * RET fd		- file descriptor of the stream created
 */
extern int slurm_init_msg_engine(slurm_addr_t *slurm_address, bool quiet);

/* slurm_accept_conn
 * accepts a incoming stream connection on a stream server slurm_fd
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr_t of the accepted connection
 * RET int		- file descriptor of the accepted connection
 */
extern int slurm_accept_conn(int open_fd, slurm_addr_t *slurm_address);
extern void *slurm_accept_msg_conn(int fd, slurm_addr_t *addr);

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address 	- slurm_addr_t of the connection destination
 * IN retry             - if true, retry as needed with various ports
 *                        to avoid socket address collision
 * RET int              - file descriptor of the connection created
 */
extern int slurm_open_stream(slurm_addr_t *slurm_address, bool retry);

/*
 * slurm_open_unix_stream
 * Opens a connection to a unix socket
 * IN addr_name - name of address of connection destination
 * IN sock_flags - flags for the socket type, see socket(2)
 * OUT fd - file descriptor of the connection on success, invalid on error
 * RET int - SLURM_SUCCESS or Slurm error code
 */
extern int slurm_open_unix_stream(char *addr_name, int sock_flags, int *fd);

/* slurm_get_stream_addr
 * essentially a encapsilated get_sockname
 * IN open_fd 		- file descriptor to retrieve slurm_addr_t for
 * OUT address		- address that open_fd to bound to
 */
extern int slurm_get_stream_addr(int open_fd, slurm_addr_t *address);

extern int slurm_send_timeout(void *tls_conn, char *buffer, size_t size,
			      int timeout);
extern int slurm_recv_timeout(void *tls_conn, char *buffer, size_t size,
			      int timeout);

/*****************************/
/* slurm addr pack functions */
/*****************************/

/* slurm_pack_addr
 * packs a slurm_addr_t into a buffer to serialization transport
 * IN addr		- slurm_addr_t to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr_t into
 */
extern void slurm_pack_addr(slurm_addr_t *addr, buf_t *buffer);

/* slurm_unpack_addr_no_alloc
 * unpacks a buffer into a slurm_addr_t after serialization transport
 * OUT addr_ptr		- slurm_addr_t to unpack to
 * IN/OUT buffer	- buffer to unpack the slurm_addr_t from
 * returns 		- Slurm error code
 */
extern int slurm_unpack_addr_no_alloc(slurm_addr_t *addr_ptr, buf_t *buffer);

#endif
