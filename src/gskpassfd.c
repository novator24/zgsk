/*
    GSK - a library to write servers

    Copyright (C) 2007 Dave Benson

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

    Contact:
        daveb@ffem.org <Dave Benson>
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <signal.h>
#include <errno.h>
#include "gskpassfd.h"
#include "gskerror.h"
#include "gskerrno.h"
#include "gskstreamlistenersocket.h"
#include "gskghelpers.h"
#include "gskutils.h"

#define max_sun_path   (sizeof(((struct sockaddr_un*)NULL)->sun_path))


/* if a socket exists but cannot be connected to,
   then delete it. XXX: test if this works right */
#define DO_DELETE_STALE_SOCKET  1

#define MAX_DATA_LEN            64*1024

/**
 * gsk_pass_fd_make_pair:
 * @sender_fd_out: place to put the file-descriptor
 * for the sending process.
 * @receiver_fd_out: place to put the file-descriptor
 * for the receiving process.
 * @error: optional location to store error at.
 *
 * Create a sender/receiver pair that are attached
 * to eachother.  This can be useful for passing
 * file-descriptors to a child process. (and vice versa)
 *
 * returns: whether the pair was created.
 * *error is set if this returns FALSE.
 */
gboolean
gsk_pass_fd_make_pair    (int          *sender_fd_out,
                          int          *receiver_fd_out,
                          GError      **error)
{
  int fds[2];
retry_socketpair:
  if (socketpair (PF_UNIX, SOCK_DGRAM, 0, fds) < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry_socketpair;
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error creating pass-fd pair: %s",
                   g_strerror (errno));
      return FALSE;
    }
  gsk_fd_set_close_on_exec (fds[0], TRUE);
  *sender_fd_out = fds[0];
  gsk_fd_set_nonblocking (fds[0]);
  gsk_fd_set_close_on_exec (fds[1], TRUE);
  *receiver_fd_out = fds[1];
  gsk_fd_set_nonblocking (fds[1]);
  return TRUE;
}

/**
 * gsk_pass_fd_make_sender:
 * @error: optional location to store error at.
 *
 * Make a sender-fd for communicating with file-descriptor receivers.
 *
 * returns: the file-descriptor, or -1 on error.
 */
int
gsk_pass_fd_make_sender  (GError      **error)
{
  int fd;
retry_socket:
  fd = socket(PF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry_socket;
      gsk_errno_fd_creation_failed ();
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "error creating pass-fd: %s",
                   g_strerror (errno));
      return -1;
    }
  gsk_fd_set_close_on_exec (fd, TRUE);
  gsk_fd_set_nonblocking (fd);
  return fd;
}

/**
 * gsk_pass_fd_bind_receiver:
 * @path: the location in the file-system to bind the
 * file-descriptor to.
 * @error: optional location to store error at.
 *
 * Make a receiver-fd for accepting new file-descriptors,
 * via a known path in the file-system.
 *
 * returns: the file-descriptor, or -1 on error.
 */
int      gsk_pass_fd_bind_receiver(const char   *path,
                                   GError      **error)
{
  int fd = gsk_pass_fd_make_sender (error);
  struct sockaddr_un addr;
  int one;
  gboolean did_mkdir = FALSE;
  if (fd < 0)
    return -1;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&one, sizeof(one));
  memset (&addr, 0, sizeof (addr));
  if (strlen (path) > max_sun_path)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_FILE_NAME_TOO_LONG,
                   "cannot bind to path of length %u: too long (max is %u)",
                   strlen (path),
                   max_sun_path);
      close (fd);
      return -1;
    }
  addr.sun_family = AF_UNIX;
  strncpy (addr.sun_path, path, max_sun_path);

  if (DO_DELETE_STALE_SOCKET)
    {
      GskSocketAddress *gskaddr = gsk_socket_address_new_local (path);
      _gsk_socket_address_local_maybe_delete_stale_socket (gskaddr);
      g_object_unref (gskaddr);
    }
retry_bind:
  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0)
    {
      int e = errno;
      if (gsk_errno_is_ignorable (e))
        goto retry_bind;
      if (e == ENOENT && !did_mkdir)
        {
          const char *last_slash = strrchr (path, '/');
          if (last_slash != NULL)
            {
              char *dir = g_strndup (path, last_slash - path);
              did_mkdir = TRUE;
              if (gsk_mkdir_p (dir, 0755, error))
                {
                  g_free (dir);
                  goto retry_bind;
                }
              g_free (dir);
            }
        }
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (e),
                   "bind(2) failed when creating a listener (%s): %s",
                   path, g_strerror (errno));
      return -1;
    }
  return fd;
}

static gboolean
send_fd (int           sender_fd,
         const char   *path,
         int           pass_fd,
         guint         aux_info_len,
         const guint8 *aux_info_data,
         GError      **error)
{
  struct msghdr msg;
  char ccmsg[CMSG_SPACE (sizeof (pass_fd))];
  struct cmsghdr *cmsg;
  struct iovec vec;		/* stupidity: must send/receive at least one byte */
  struct sockaddr_un addr;

  if (aux_info_len == 0)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "aux-info-len must be nonzero");
      return FALSE;
    }
  if (aux_info_len > MAX_DATA_LEN)
    {
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   GSK_ERROR_INVALID_ARGUMENT,
                   "cannot pass a buffer of more than %u bytes as aux-data",
                   MAX_DATA_LEN);
      return FALSE;
    }

  if (path == NULL)
    {
      msg.msg_name = NULL;
      msg.msg_namelen = 0;
    }
  else
    {
      if (strlen (path) > max_sun_path)
        {
          g_set_error (error, GSK_G_ERROR_DOMAIN,
                       GSK_ERROR_FILE_NAME_TOO_LONG,
                       "send_fd: path too long at %u chars (max is %u chars)",
                       strlen (path), max_sun_path);
          return FALSE;
        }
      memset (&addr, 0, sizeof (addr));
      addr.sun_family = AF_UNIX;
      strncpy (addr.sun_path, path, max_sun_path);
      msg.msg_name = (struct sockaddr *) &addr;
      msg.msg_namelen = sizeof (struct sockaddr_un);
    }

  vec.iov_base = (void *) aux_info_data;
  vec.iov_len = aux_info_len;
  msg.msg_iov = &vec;
  msg.msg_iovlen = 1;

  /* old BSD implementations should use msg_accrights instead of 
   * msg_control; the interface is different. */
  msg.msg_control = ccmsg;
  msg.msg_controllen = sizeof (ccmsg);
  cmsg = CMSG_FIRSTHDR (&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN (sizeof (pass_fd));
  *(int *) CMSG_DATA (cmsg) = pass_fd;
  msg.msg_controllen = cmsg->cmsg_len;

  msg.msg_flags = 0;

  if (sendmsg (sender_fd, &msg, 0) < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        return FALSE;
      g_set_error (error, GSK_G_ERROR_DOMAIN,
                   gsk_error_code_from_errno (errno),
                   "sendmsg failed (passing fd %d through fd %d): %s",
                   pass_fd, sender_fd, g_strerror (errno));
      return FALSE;
    }
  return TRUE;
}

/**
 * gsk_pass_fd_send:
 * @sender_fd: the file-descriptor allocated with
 * gsk_pass_fd_make_pair().
 * @pass_fd: the file-descriptor to pass.
 * @aux_info_len: length of auxiliary information to send with the fd.
 * must be nonzero!
 * @aux_info_data: auxiliary information to send with the fd.
 * @error: optional location to store error at.
 *
 * Pass a file-descriptor from one process to another
 * using a file-descriptor that does not require a path.
 *
 * This function can fail transiently (if there is no space
 * in the message-queue), in which case it returns FALSE
 * but does NOT set *error.
 *
 * If this function returns TRUE, you should close pass_fd immediately.
 *
 * returns: whether the file-descriptor was passed to the subprocess.
 * If this fails, it may set *error, if the error was a serious one
 * (for example if the remote side shuts down).
 */
gboolean gsk_pass_fd_send         (int           sender_fd,
                                   int           pass_fd,
                                   guint         aux_info_len,
                                   const guint8 *aux_info_data,
                                   GError      **error)
{
  return send_fd (sender_fd, NULL, pass_fd, aux_info_len, aux_info_data, error);
}
/**
 * gsk_pass_fd_sendto:
 * @sender_fd: the file-descriptor allocated with
 * gsk_pass_fd_make_sender().
 * @path: the location to send to file-descriptor to.
 * @pass_fd: the file-descriptor to pass.
 * @aux_info_len: length of auxiliary information to send with the fd.
 * must be nonzero!
 * @aux_info_data: auxiliary information to send with the fd.
 * @error: optional location to store error at.
 *
 * Pass a file-descriptor from one process to a named location.
 * The name should match a location where some other process
 * has called gsk_pass_fd_bind_receiver().
 *
 * This function can fail transiently (if there is no space
 * in the message-queue), in which case it returns FALSE
 * but does NOT set *error.
 *
 * If this function returns TRUE, you should close pass_fd immediately.
 *
 * returns: whether the file-descriptor was passed to the subprocess.
 * If this fails, it may set *error, if the error was a serious one
 * (for example if the remote side shuts down).
 */
gboolean gsk_pass_fd_sendto       (int           sender_fd,
                                   const char   *path,
                                   int           pass_fd,
                                   guint         aux_info_len,
                                   const guint8 *aux_info_data,
                                   GError      **error)
{
  g_return_val_if_fail (path != NULL, FALSE);
  return send_fd (sender_fd, path, pass_fd, aux_info_len, aux_info_data, error);
}

/**
 * gsk_pass_fd_receive:
 * @receiver_fd: the file-descriptor from whence to receive
 * a new file-descriptor.
 * @aux_info_len_out: optional location to store the length of the aux data.
 * @aux_info_data_out: optional location to store a newly allocated
 * copy of the aux-data.  If you provide this, then you must g_free the buffer!
 * @error: optional location to store error at.
 *
 * Receive a file-descriptor from a receiver-fd
 * which was allocated either via gsk_pass_fd_make_pair()
 * or gsk_pass_fd_bind_receiver().
 *
 * This function can fail transiently (if there is no file-descriptor
 * in the message-queue), in which case it returns FALSE
 * but does NOT set *error.
 *
 * Generally you should only call this when the file-descriptor is
 * readable (using gsk_main_loop_add_io()).
 *
 * returns: the file-descriptor received, or -1 if a failure occurred.
 * If -1 is returns, *error will be set if the error is considered serious.
 */
int
gsk_pass_fd_receive      (int           receiver_fd,
                          guint        *aux_info_len_out,
                          guint8      **aux_info_data_out,
                          GError      **error)
{
  struct msghdr msg;
  struct iovec iov;
  guint8 aux_info_buf[MAX_DATA_LEN];
  int rv;
  int connfd = -1;
  char ccmsg[CMSG_SPACE (sizeof (connfd))];
  struct cmsghdr *cmsg;
  int fd;

  iov.iov_base = aux_info_buf;
  iov.iov_len = MAX_DATA_LEN;

  msg.msg_name = 0;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  /* old BSD implementations should use msg_accrights instead of 
   * msg_control; the interface is different. */
  msg.msg_control = ccmsg;
  msg.msg_controllen = sizeof (ccmsg);	/* ? seems to work... */

  rv = recvmsg (receiver_fd, &msg, 0);
  if (rv == -1)
    {
      g_warning ("recvmsg failed: %s", g_strerror (errno));
      return -1;
    }

  cmsg = CMSG_FIRSTHDR (&msg);
  if (!cmsg->cmsg_type == SCM_RIGHTS)
    {
      g_error ("got control message of unknown type %d",
	       cmsg->cmsg_type);
      return -1;
    }
  fd = *(int *) CMSG_DATA (cmsg);
  gsk_fd_set_nonblocking (fd);  /* kinda unclear if this is a good idea */

  if (aux_info_len_out != NULL)
    *aux_info_len_out = rv;
  if (aux_info_data_out != NULL)
    *aux_info_data_out = g_memdup (iov.iov_base, rv);
  return fd;
}
