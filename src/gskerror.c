#include "gskerror.h"
#include <errno.h>

GType gsk_error_code_type = 0;
GQuark gsk_g_error_domain_quark = 0;


static GType
gsk_error_code_get_type ()
{
  static GType enum_type = 0;
  if (enum_type == 0)
    {
      static const GEnumValue enum_values[] = {
#include "gskerror.inc"
	{0,NULL,NULL}
      };
      enum_type = g_enum_register_static ("GskErrorCode", enum_values);
    }
  return enum_type;
}

static struct {
  int errno_value;
  GskErrorCode code;
} errno_codes[] = {
#ifdef EPERM
{ EPERM, GSK_ERROR_PERMISSION_DENIED },
#endif
#ifdef ENOENT
{ ENOENT, GSK_ERROR_FILE_NOT_FOUND },
#endif
#ifdef ESRCH
{ ESRCH, GSK_ERROR_PROCESS_NOT_FOUND },
#endif
#ifdef EINTR
{ EINTR, GSK_ERROR_INTERRUPTED_SYSTEM_CALL },
#endif
#ifdef EIO
{ EIO, GSK_ERROR_DEVICE_FAILED },
#endif
#ifdef ENXIO
{ ENXIO, GSK_ERROR_DEVICE_NOT_CONFIGURED },
#endif
#ifdef E2BIG
{ E2BIG, GSK_ERROR_ARG_LIST_TOO_LONG },
#endif
#ifdef ENOEXEC
{ ENOEXEC, GSK_ERROR_BAD_FORMAT },
#endif
#ifdef EBADF
{ EBADF, GSK_ERROR_BAD_FD },
#endif
#ifdef ECHILD
{ ECHILD, GSK_ERROR_NO_CHILD_PROCESSES },
#endif
#ifdef EDEADLK
{ EDEADLK, GSK_ERROR_RESOURCE_DEADLOCK },
#endif
#ifdef ENOMEM
{ ENOMEM, GSK_ERROR_OUT_OF_MEMORY },
#endif
#ifdef EACCES
{ EACCES, GSK_ERROR_PERMISSION_DENIED },
#endif
#ifdef EFAULT
{ EFAULT, GSK_ERROR_BAD_ADDRESS },
#endif
#ifdef ENOTBLK
{ ENOTBLK, GSK_ERROR_BLOCK_DEVICE_REQUIRED },
#endif
#ifdef EBUSY
{ EBUSY, GSK_ERROR_DEVICE_BUSY },
#endif
#ifdef EEXIST
{ EEXIST, GSK_ERROR_FILE_EXISTS },
#endif
#ifdef EXDEV
{ EXDEV, GSK_ERROR_CROSS_DEVICE_LINK },
#endif
#ifdef ENODEV
{ ENODEV, GSK_ERROR_OPERATION_NOT_SUPPORTED },
#endif
#ifdef ENOTDIR
{ ENOTDIR, GSK_ERROR_NOT_A_DIRECTORY },
#endif
#ifdef EISDIR
{ EISDIR, GSK_ERROR_IS_A_DIRECTORY },
#endif
#ifdef EINVAL
{ EINVAL, GSK_ERROR_INVALID_ARGUMENT },
#endif
#ifdef ENFILE
{ ENFILE, GSK_ERROR_TOO_MANY_OPEN_FILES_IN_SYSTEM },
#endif
#ifdef EMFILE
{ EMFILE, GSK_ERROR_TOO_MANY_OPEN_FILES },
#endif
#ifdef ENOTTY
{ ENOTTY, GSK_ERROR_INAPPROPRIATE_IOCTL_FOR_DEVICE },
#endif
#ifdef ETXTBSY
{ ETXTBSY, GSK_ERROR_TEXT_FILE_BUSY },
#endif
#ifdef EFBIG
{ EFBIG, GSK_ERROR_FILE_TOO_LARGE },
#endif
#ifdef ENOSPC
{ ENOSPC, GSK_ERROR_NO_SPACE_LEFT_ON_DEVICE },
#endif
#ifdef ESPIPE
{ ESPIPE, GSK_ERROR_ILLEGAL_SEEK },
#endif
#ifdef EROFS
{ EROFS, GSK_ERROR_READ_ONLY_FILE_SYSTEM },
#endif
#ifdef EMLINK
{ EMLINK, GSK_ERROR_TOO_MANY_LINKS },
#endif
#ifdef EPIPE
{ EPIPE, GSK_ERROR_BROKEN_PIPE },
#endif
#ifdef EDOM
{ EDOM, GSK_ERROR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN },
#endif
#ifdef ERANGE
{ ERANGE, GSK_ERROR_RESULT_TOO_LARGE },
#endif
#ifdef EAGAIN
{ EAGAIN, GSK_ERROR_RESOURCE_TEMPORARILY_UNAVAILABLE },
#endif
#ifdef EINPROGRESS
{ EINPROGRESS, GSK_ERROR_OPERATION_NOW_IN_PROGRESS },
#endif
#ifdef EALREADY
{ EALREADY, GSK_ERROR_OPERATION_ALREADY_IN_PROGRESS },
#endif
#ifdef ENOTSOCK
{ ENOTSOCK, GSK_ERROR_SOCKET_OPERATION_ON_NON_SOCKET },
#endif
#ifdef EDESTADDRREQ
{ EDESTADDRREQ, GSK_ERROR_DESTINATION_ADDRESS_REQUIRED },
#endif
#ifdef EMSGSIZE
{ EMSGSIZE, GSK_ERROR_MESSAGE_TOO_LONG },
#endif
#ifdef EPROTOTYPE
{ EPROTOTYPE, GSK_ERROR_PROTOCOL_WRONG_TYPE_FOR_SOCKET },
#endif
#ifdef ENOPROTOOPT
{ ENOPROTOOPT, GSK_ERROR_PROTOCOL_NOT_AVAILABLE },
#endif
#ifdef EPROTONOSUPPORT
{ EPROTONOSUPPORT, GSK_ERROR_PROTOCOL_NOT_SUPPORTED },
#endif
#ifdef ESOCKTNOSUPPORT
{ ESOCKTNOSUPPORT, GSK_ERROR_SOCKET_TYPE_NOT_SUPPORTED },
#endif
#ifdef EOPNOTSUPP
{ EOPNOTSUPP, GSK_ERROR_OPERATION_NOT_SUPPORTED },
#endif
#ifdef ENOTSUP
{ ENOTSUP, GSK_ERROR_OPERATION_NOT_SUPPORTED },
#endif
#ifdef EPFNOSUPPORT
{ EPFNOSUPPORT, GSK_ERROR_PROTOCOL_FAMILY_NOT_SUPPORTED },
#endif
#ifdef EAFNOSUPPORT
{ EAFNOSUPPORT, GSK_ERROR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY },
#endif
#ifdef EADDRINUSE
{ EADDRINUSE, GSK_ERROR_ADDRESS_ALREADY_IN_USE },
#endif
#ifdef EADDRNOTAVAIL
{ EADDRNOTAVAIL, GSK_ERROR_CANNOT_ASSIGN_REQUESTED_ADDRESS },
#endif
#ifdef ENETDOWN
{ ENETDOWN, GSK_ERROR_NETWORK_IS_DOWN },
#endif
#ifdef ENETUNREACH
{ ENETUNREACH, GSK_ERROR_NETWORK_IS_UNREACHABLE },
#endif
#ifdef ENETRESET
{ ENETRESET, GSK_ERROR_NETWORK_DROPPED_CONNECTION_ON_RESET },
#endif
#ifdef ECONNABORTED
{ ECONNABORTED, GSK_ERROR_SOFTWARE_CAUSED_CONNECTION_ABORT },
#endif
#ifdef ECONNRESET
{ ECONNRESET, GSK_ERROR_CONNECTION_RESET_BY_PEER },
#endif
#ifdef ENOBUFS
{ ENOBUFS, GSK_ERROR_NO_BUFFER_SPACE_AVAILABLE },
#endif
#ifdef EISCONN
{ EISCONN, GSK_ERROR_SOCKET_IS_ALREADY_CONNECTED },
#endif
#ifdef ENOTCONN
{ ENOTCONN, GSK_ERROR_SOCKET_IS_NOT_CONNECTED },
#endif
#ifdef ESHUTDOWN
{ ESHUTDOWN, GSK_ERROR_CANNOT_SEND_AFTER_SOCKET_SHUTDOWN },
#endif
#ifdef ETOOMANYREFS
{ ETOOMANYREFS, GSK_ERROR_TOO_MANY_REFERENCES_CANNOT_SPLICE },
#endif
#ifdef ETIMEDOUT
{ ETIMEDOUT, GSK_ERROR_OPERATION_TIMED_OUT },
#endif
#ifdef ECONNREFUSED
{ ECONNREFUSED, GSK_ERROR_CONNECTION_REFUSED },
#endif
#ifdef ELOOP
{ ELOOP, GSK_ERROR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINKS },
#endif
#ifdef ENAMETOOLONG
{ ENAMETOOLONG, GSK_ERROR_FILE_NAME_TOO_LONG },
#endif
#ifdef EHOSTDOWN
{ EHOSTDOWN, GSK_ERROR_HOST_IS_DOWN },
#endif
#ifdef EHOSTUNREACH
{ EHOSTUNREACH, GSK_ERROR_NO_ROUTE_TO_HOST },
#endif
#ifdef ENOTEMPTY
{ ENOTEMPTY, GSK_ERROR_DIRECTORY_NOT_EMPTY },
#endif
#ifdef EPROCLIM
{ EPROCLIM, GSK_ERROR_TOO_MANY_PROCESSES },
#endif
#ifdef EUSERS
{ EUSERS, GSK_ERROR_TOO_MANY_USERS },
#endif
#ifdef EDQUOT
{ EDQUOT, GSK_ERROR_DISC_QUOTA_EXCEEDED },
#endif
#ifdef ESTALE
{ ESTALE, GSK_ERROR_STALE_NFS_FILE_HANDLE },
#endif
#ifdef EREMOTE
{ EREMOTE, GSK_ERROR_TOO_MANY_LEVELS_OF_REMOTE_IN_PATH },
#endif
#ifdef EBADRPC
{ EBADRPC, GSK_ERROR_RPC_STRUCT_IS_BAD },
#endif
#ifdef ERPCMISMATCH
{ ERPCMISMATCH, GSK_ERROR_RPC_VERSION_WRONG },
#endif
#ifdef EPROGUNAVAIL
{ EPROGUNAVAIL, GSK_ERROR_RPC_PROG_NOT_AVAIL },
#endif
#ifdef EPROGMISMATCH
{ EPROGMISMATCH, GSK_ERROR_PROGRAM_VERSION_WRONG },
#endif
#ifdef EPROCUNAVAIL
{ EPROCUNAVAIL, GSK_ERROR_BAD_PROCEDURE_FOR_PROGRAM },
#endif
#ifdef ENOLCK
{ ENOLCK, GSK_ERROR_NO_LOCKS_AVAILABLE },
#endif
#ifdef ENOSYS
{ ENOSYS, GSK_ERROR_FUNCTION_NOT_IMPLEMENTED }
#endif
};

static GHashTable *errno_hash_table = NULL;

/* TODO: get rid of this, by replacing it with a bsearchable
 * table by some magical autogeneration */
void
_gsk_error_init (void)
{
  if (errno_hash_table == NULL)
    {
      guint i;
      errno_hash_table = g_hash_table_new (NULL, NULL);
      for (i = 0; i < G_N_ELEMENTS (errno_codes); i++)
	g_hash_table_insert (errno_hash_table,
			     GUINT_TO_POINTER (errno_codes[i].errno_value),
			     GUINT_TO_POINTER (errno_codes[i].code));
    }
  gsk_error_code_type = gsk_error_code_get_type ();
  gsk_g_error_domain_quark = g_quark_from_static_string ("GskError");
}
      
/**
 * gsk_error_code_from_errno:
 * @errno_number: a value of the same type as returned in errno.
 *
 * Translates an errno code into a GskErrorCode.
 * 
 * returns: the relevant GskErrorCode, or 0, if Gsk doesn't support
 * an errno returned by your OS.
 */
GskErrorCode
gsk_error_code_from_errno (int errno_number)
{
  return GPOINTER_TO_UINT (g_hash_table_lookup (errno_hash_table,
						GUINT_TO_POINTER (errno_number)));
}
