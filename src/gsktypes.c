#include "gsktypes.h"

/* --- file-descriptor parameters ---- */

GType
gsk_fd_get_type ()
{
  static GType type = 0;
  if (type == 0)
    {
      static const GTypeInfo type_info = {
	0,                /* class_size */
	NULL,             /* base_init */
	NULL,             /* base_finalize */
	NULL,             /* class_init */
	NULL,             /* class_finalize */
	NULL,             /* class_data */
	0,                /* instance_size */
	0,                /* n_preallocs */
	0,                /* instance_init */
	NULL
	};
      type = g_type_register_static (G_TYPE_INT,
				     "GskFd",
				     &type_info,
				     0);
    }
  return type;
}

typedef struct _GskParamSpecFd GskParamSpecFd;
struct _GskParamSpecFd
{
  GParamSpec parent_instance;
};

static void
param_fd_init (GParamSpec *pspec)
{
  /* nothing to do */
}

static void
param_fd_set_default (GParamSpec *pspec,
		      GValue     *value)
{ 
  g_value_set_int (value, -1);
}

static gboolean
param_fd_validate (GParamSpec *pspec,
		   GValue     *value)
{
  int fd = g_value_get_int (value);
  if (fd < -1)
    {
      g_value_set_int (value, -1);
      return TRUE;
    }
  return FALSE;
}

static gint
param_fd_values_cmp (GParamSpec   *pspec,
		     const GValue *value1,
		     const GValue *value2)
{
  int fd1 = g_value_get_int (value1);
  int fd2 = g_value_get_int (value2);
  return (fd1 < fd2) ? -1 : (fd1 > fd2) ? +1 : 0;
}

GType
gsk_param_fd_get_type ()
{
  static GType type = 0;
  if (type == 0)
    {
      static GParamSpecTypeInfo pspec_info = {
	sizeof (GskParamSpecFd),  /* instance_size */
	4,                        /* n_preallocs */
	param_fd_init,            /* instance_init */
	0,              	  /* value_type */
	NULL,                     /* finalize */
	param_fd_set_default,     /* value_set_default */
	param_fd_validate,        /* value_validate */
	param_fd_values_cmp,      /* values_cmp */
      };
      pspec_info.value_type = GSK_TYPE_FD;
      type = g_param_type_register_static ("GskParamFd", &pspec_info);
    }
  return type;
}


GParamSpec *
gsk_param_spec_fd (const char *name,
		   const char *nick,
		   const char *blurb,
		   GParamFlags flags)
{
  return g_param_spec_internal (GSK_TYPE_PARAM_FD, name, nick, blurb, flags);
}
