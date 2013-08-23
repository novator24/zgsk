#ifndef __GSK_IO_H_
#define __GSK_IO_H_

#include <glib-object.h>
#include "gskhook.h"
#include "gskerror.h"

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskIO GskIO;
typedef struct _GskIOClass GskIOClass;

typedef gboolean (*GskIOHookFunc) (GskIO        *io,
				   gpointer      data);
/* --- type macros --- */
GType gsk_io_get_type(void) G_GNUC_CONST;
#define GSK_TYPE_IO		 (gsk_io_get_type ())
#define GSK_IO(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_IO, GskIO))
#define GSK_IO_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_IO, GskIOClass))
#define GSK_IO_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_IO, GskIOClass))
#define GSK_IS_IO(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_IO))
#define GSK_IS_IO_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_IO))

typedef enum
{
  GSK_IO_ERROR_NONE,
  GSK_IO_ERROR_INIT,
  GSK_IO_ERROR_CONNECT,
  GSK_IO_ERROR_OPEN,
  GSK_IO_ERROR_READ,
  GSK_IO_ERROR_WRITE,
  GSK_IO_ERROR_POLL_READ,
  GSK_IO_ERROR_POLL_WRITE,
  GSK_IO_ERROR_SHUTDOWN_READ,
  GSK_IO_ERROR_SHUTDOWN_WRITE,
  GSK_IO_ERROR_CLOSE,
  GSK_IO_ERROR_SYNC,
  GSK_IO_ERROR_POLL
} GskIOErrorCause;
const char * gsk_io_error_cause_to_string (GskIOErrorCause cause);

/* --- structures --- */
struct _GskIOClass 
{
  GObjectClass object_class;
  /* --- signals (do not override, usually) --- */
  /* Emitted after the connection is made. */
  void       (*on_connect)      (GskIO      *io);
  void       (*on_error)        (GskIO      *io);

  /* --- virtuals --- */
  gboolean   (*open)            (GskIO      *io,
				 GError    **error);
  void       (*set_poll_read)   (GskIO      *io,
				 gboolean    do_poll);
  void       (*set_poll_write)  (GskIO      *io,
				 gboolean    do_poll);
  gboolean   (*shutdown_read)   (GskIO      *io,
				 GError    **error);
  gboolean   (*shutdown_write)  (GskIO      *io,
				 GError    **error);
  void       (*close)           (GskIO      *io);
};
struct _GskIO 
{
  GObject      object;

  guint        is_connecting : 1;
  guint        is_open : 1;
  guint        open_failed : 1;
  guint        shutdown_on_error : 1;

  /*< private >*/
  guint        in_idle_ready_thread : 1;

  /*< public read-only >*/
  guint        error_cause : 4;

  /*< public read-write >*/
  guint        print_errors : 1;

  /*< public read-only >*/
  GError      *error;

  /*< private >*/
  /* hooks */
  GskHook           read_hook;
  GskHook           write_hook;
};
/* --- prototypes --- */

/* public */
#define GSK_IO_READ_HOOK(io)		((GskHook*) &GSK_IO (io)->read_hook)
#define GSK_IO_WRITE_HOOK(io)		((GskHook*) &GSK_IO (io)->write_hook)
#define gsk_io_block_read(io)		gsk_hook_block (GSK_IO_READ_HOOK (io))
#define gsk_io_block_write(io)		gsk_hook_block (GSK_IO_WRITE_HOOK (io))
#define gsk_io_unblock_read(io)		gsk_hook_unblock (GSK_IO_READ_HOOK (io))
#define gsk_io_unblock_write(io)	gsk_hook_unblock (GSK_IO_WRITE_HOOK (io))
#define gsk_io_has_read_hook(io)        gsk_hook_is_trapped (GSK_IO_READ_HOOK (io))
#define gsk_io_has_write_hook(io)       gsk_hook_is_trapped (GSK_IO_WRITE_HOOK (io))
#define gsk_io_trap_readable(io, func, shutdown_func, data, destroy)		\
	gsk_hook_trap (GSK_IO_READ_HOOK (io),					\
		       (GskHookFunc) func, (GskHookFunc) shutdown_func, data, destroy)
#define gsk_io_trap_writable(io, func, shutdown_func, data, destroy)		\
	gsk_hook_trap (GSK_IO_WRITE_HOOK (io),					\
		       (GskHookFunc) func, (GskHookFunc) shutdown_func, data, destroy)
#define gsk_io_untrap_readable(io) gsk_hook_untrap (GSK_IO_READ_HOOK (io))
#define gsk_io_untrap_writable(io) gsk_hook_untrap (GSK_IO_WRITE_HOOK (io))
void gsk_io_shutdown (GskIO   *io,
		      GError **error);

/* shutdown the io in various ways */             
#define gsk_io_read_shutdown(io, error)   gsk_hook_shutdown (GSK_IO_READ_HOOK (io), error)
#define gsk_io_write_shutdown(io, error)  gsk_hook_shutdown (GSK_IO_WRITE_HOOK (io), error)
void     gsk_io_close             (GskIO          *io);


/* protected: do notifications of the usual events */
#define gsk_io_notify_ready_to_read(io)		gsk_hook_notify (GSK_IO_READ_HOOK (io))
#define gsk_io_notify_ready_to_write(io)	gsk_hook_notify (GSK_IO_WRITE_HOOK (io))
#define gsk_io_notify_read_shutdown(io)		gsk_hook_notify_shutdown (GSK_IO_READ_HOOK (io))
#define gsk_io_notify_write_shutdown(io)	gsk_hook_notify_shutdown (GSK_IO_WRITE_HOOK (io))
void     gsk_io_notify_shutdown (GskIO *io);
void     gsk_io_notify_connected (GskIO *io);

/* flags */
#define gsk_io_get_is_connecting(io)             _GSK_IO_TEST_FIELD (io, is_connecting)
#define gsk_io_get_is_readable(io)               _GSK_IO_TEST_READ_FLAG (io, IS_AVAILABLE)
#define gsk_io_get_is_writable(io)               _GSK_IO_TEST_WRITE_FLAG (io, IS_AVAILABLE)
#define gsk_io_get_is_read_shutting_down(io)     GSK_HOOK_TEST_SHUTTING_DOWN (GSK_IO_READ_HOOK (io))
#define gsk_io_get_is_write_shutting_down(io)    GSK_HOOK_TEST_SHUTTING_DOWN (GSK_IO_WRITE_HOOK (io))
#define gsk_io_get_never_partial_reads(io)       _GSK_IO_TEST_FIELD (io, never_partial_reads)
#define gsk_io_get_never_partial_writes(io)      _GSK_IO_TEST_FIELD (io, never_partial_writes)
#define gsk_io_get_never_blocks_write(io)        GSK_HOOK_TEST_NEVER_BLOCKS (GSK_IO_WRITE_HOOK (io))
#define gsk_io_get_never_blocks_read(io)         GSK_HOOK_TEST_NEVER_BLOCKS (GSK_IO_READ_HOOK (io))
#define gsk_io_get_idle_notify_write(io)         GSK_HOOK_TEST_IDLE_NOTIFY (GSK_IO_WRITE_HOOK (io))
#define gsk_io_get_idle_notify_read(io)          GSK_HOOK_TEST_IDLE_NOTIFY (GSK_IO_READ_HOOK (io))
#define gsk_io_get_is_open(io)                   _GSK_IO_TEST_FIELD (io, is_open)
#define gsk_io_get_shutdown_on_error(io)         _GSK_IO_TEST_FIELD (io, shutdown_on_error)

/* protected */
#define gsk_io_set_idle_notify_read(io,v)        gsk_hook_set_idle_notify (GSK_IO_READ_HOOK (io),v)
#define gsk_io_set_idle_notify_write(io,v)       gsk_hook_set_idle_notify (GSK_IO_WRITE_HOOK (io),v)
#define gsk_io_mark_is_connecting(io)            _GSK_IO_MARK_FIELD (io, is_connecting)
#define gsk_io_mark_is_readable(io)              _GSK_IO_SET_READ_FLAG (io, IS_AVAILABLE)
#define gsk_io_mark_is_writable(io)              _GSK_IO_SET_WRITE_FLAG (io, IS_AVAILABLE)
#define gsk_io_mark_never_partial_reads(io)      _GSK_IO_MARK_FIELD (io, never_partial_reads)
#define gsk_io_mark_never_partial_writes(io)     _GSK_IO_MARK_FIELD (io, never_partial_writes)
#define gsk_io_mark_shutdown_on_error(io)        _GSK_IO_MARK_FIELD (io, shutdown_on_error)
#define gsk_io_mark_never_blocks_write(io)       gsk_hook_mark_never_blocks (GSK_IO_WRITE_HOOK (io))
#define gsk_io_mark_never_blocks_read(io)        gsk_hook_mark_never_blocks (GSK_IO_READ_HOOK (io))
#define gsk_io_mark_idle_notify_write(io)        gsk_hook_mark_idle_notify (GSK_IO_WRITE_HOOK (io))
#define gsk_io_mark_idle_notify_read(io)         gsk_hook_mark_idle_notify (GSK_IO_READ_HOOK (io))
#define gsk_io_mark_is_open(io)                  _GSK_IO_MARK_FIELD (io, is_open)
#define gsk_io_clear_is_readable(io)             _GSK_IO_CLEAR_READ_FLAG (io, IS_AVAILABLE)
#define gsk_io_clear_is_writable(io)             _GSK_IO_CLEAR_WRITE_FLAG (io, IS_AVAILABLE)
#define gsk_io_clear_never_partial_reads(io)     _GSK_IO_CLEAR_FIELD (io, never_partial_reads)
#define gsk_io_clear_never_partial_writes(io)    _GSK_IO_CLEAR_FIELD (io, never_partial_writes)
#define gsk_io_clear_idle_notify_write(io)       gsk_hook_clear_idle_notify (GSK_IO_WRITE_HOOK (io))
#define gsk_io_clear_idle_notify_read(io)        gsk_hook_clear_idle_notify (GSK_IO_READ_HOOK (io))
#define gsk_io_clear_is_open(io)                 _GSK_IO_CLEAR_FIELD (io, is_open)
#define gsk_io_clear_shutdown_on_error(io)       _GSK_IO_CLEAR_FIELD (io, shutdown_on_error)
#define gsk_io_is_polling_for_read(io)           gsk_hook_get_last_poll_state (GSK_IO_READ_HOOK (io))
#define gsk_io_is_polling_for_write(io)          gsk_hook_get_last_poll_state (GSK_IO_WRITE_HOOK (io))




/* --- error handling --- */

/* these functions are for use by derived classes only. */
void        gsk_io_set_error (GskIO             *io,
                              GskIOErrorCause    cause,
                              GskErrorCode       error_code,
                              const char        *format,
                              ...) G_GNUC_PRINTF(4,5);
void        gsk_io_set_gerror (GskIO             *io,
                               GskIOErrorCause    cause,
                               GError            *error);

void        gsk_io_set_default_print_errors (gboolean print_errors);

/*< private >*/
void _gsk_io_stop_idle_ready (GskIO *io);
void _gsk_io_make_idle_ready (GskIO *io);
void _gsk_io_nonblocking_init ();

/* implementation bits */
#define _GSK_IO_TEST_FIELD(io, field)	        (GSK_IO (io)->field != 0)
#define _GSK_IO_TEST_READ_FLAG(io, flag)        GSK_HOOK_TEST_FLAG(GSK_IO_READ_HOOK(io), flag)
#define _GSK_IO_TEST_WRITE_FLAG(io, flag)       GSK_HOOK_TEST_FLAG(GSK_IO_WRITE_HOOK(io), flag)
#define _GSK_IO_MARK_FIELD(io, field)	        G_STMT_START{ GSK_IO (io)->field = 1; }G_STMT_END
#define _GSK_IO_MARK_READ_FLAG(io, flag)        GSK_HOOK_MARK_FLAG(GSK_IO_READ_HOOK(io), flag)
#define _GSK_IO_MARK_WRITE_FLAG(io, flag)       GSK_HOOK_MARK_FLAG(GSK_IO_WRITE_HOOK(io), flag)
#define _GSK_IO_CLEAR_FIELD(io, field)	        G_STMT_START{ GSK_IO (io)->field = 0; }G_STMT_END
#define _GSK_IO_CLEAR_READ_FLAG(io, flag)       GSK_HOOK_CLEAR_FLAG(GSK_IO_READ_HOOK(io), flag)
#define _GSK_IO_CLEAR_WRITE_FLAG(io, flag)      GSK_HOOK_CLEAR_FLAG(GSK_IO_WRITE_HOOK(io), flag)

#ifndef GSK_DISABLE_DEPRECATED
#define _GSK_IO_SET_READ_FLAG _GSK_IO_MARK_READ_FLAG
#define _GSK_IO_SET_WRITE_FLAG _GSK_IO_MARK_WRITE_FLAG
#endif

G_END_DECLS

#endif
