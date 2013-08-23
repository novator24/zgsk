#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "../gskpassfd.h"
#include "../gskghelpers.h"
#include "../gskutils.h"
#include "../gskerrno.h"

#define AUX_DATA_STRING "this is aux data"
#define AUX_DATA_LEN    strlen(AUX_DATA_STRING)
#define AUX_DATA        ((guint8*) AUX_DATA_STRING)

int do_fork (void)
{
  int rv;
retry_fork:
  rv = fork ();
  if (rv < 0)
    {
      if (gsk_errno_is_ignorable (errno))
        goto retry_fork;
      g_error ("fork failed: %s", g_strerror (errno));
    }
  return rv;
}

int main(int argc, char **argv)
{
  int send_fd, receive_fd;
  guint count;
  GError *error = NULL;


  /* test gsk_pass_fd_make_pair() mode */
  for (count = 0; count < 3; count++)
    {
      int pid;
      int pipe_fds[2];
      int status;
      g_printerr ("Anon passing, iter %u... ", count);
      if (!gsk_pass_fd_make_pair (&send_fd, &receive_fd, &error))
	g_error ("error making pair of file-descriptors: %s", error->message);
      pid = do_fork ();
      if (pid == 0)
        {
	  char buf[7];
          int fd;
          guint msg_len;
          guint8 *msg;
	  close (send_fd);
	  gsk_fd_clear_nonblocking (receive_fd);
	  retry:
	  fd = gsk_pass_fd_receive (receive_fd, &msg_len, &msg, &error);
	  if (fd < 0)
	    {
	      if (error == NULL)
	        goto retry;
	      g_error ("error receiving fd: %s", error->message);
	    }
	  gsk_fd_clear_nonblocking (fd);
	  if (gsk_readn (fd, buf, 7) != 7
	   || memcmp (buf, "hi mom\n", 7) != 0)
	    g_error ("child for count %u failed", count);
          if (msg_len != AUX_DATA_LEN 
           || memcmp (msg, AUX_DATA, AUX_DATA_LEN) != 0)
           g_error ("aux-data mismatch");
          close (fd);
	  close (receive_fd);
	  _exit(0);
	}
      if (pipe (pipe_fds) < 0)
	g_error ("error running pipe: %s", g_strerror (errno));
      close (receive_fd);
      gsk_fd_clear_nonblocking (send_fd);

      /* XXX: race condition */
      sleep(1);

      if (!gsk_pass_fd_send (send_fd, pipe_fds[0], AUX_DATA_LEN, AUX_DATA, &error))
        g_error ("gsk_pass_fd_send failed: %s",
	         error ? error->message : "transient error");
      close (pipe_fds[0]);
      
      if (gsk_writen (pipe_fds[1], "hi mom\n", 7) != 7)
        g_error ("error writing to pipe");
      close (pipe_fds[1]);

      if (waitpid (pid, &status, 0) < 0)
        g_error ("error running waitpid: %s", g_strerror (errno));
      if (status != 0)
        g_error ("waitpid status was %u", status);
      close (send_fd);
      g_printerr ("ok.\n");
    }
  return 0;
}
