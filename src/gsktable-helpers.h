
#ifdef MAX_PATH
# define GSK_TABLE_MAX_PATH MAX_PATH
#else
# define GSK_TABLE_MAX_PATH 4096
#endif
#define gsk_table_mk_fname(fname_buf, dir, id_int64, extension) \
  g_snprintf (fname_buf, GSK_TABLE_MAX_PATH, "%s/%"G_GINT64_MODIFIER"x.%s", (dir), (guint64) (id_int64), (extension))
