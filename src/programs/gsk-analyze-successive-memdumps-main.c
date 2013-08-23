#include <stdio.h>
#include <glib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../gskutils.h"

typedef struct _AllocInfo AllocInfo;
typedef struct _Context Context;
typedef struct _CodePoint CodePoint;

struct _AllocInfo
{
  unsigned n_blocks, n_bytes;
};

struct _Context
{
  char *str;
  AllocInfo *alloc_infos;
  guint64 total_bytes;
};

void update_percent_bar(unsigned value,
                        unsigned n_values)
{
  if (value == 0)
    {
      if (n_values == 0)
        g_printerr ("done.\n");
      else
        g_printerr ("0%%");
    }
  else
    {
      guint old_per = 100 * (value-1) / n_values;
      guint new_per = 100 * (value) / n_values;

      if (old_per == new_per && value < n_values)
        return;

      if (old_per < 10)
        g_printerr ("\10\10");
      else
        g_printerr ("\10\10\10");
      if (n_values == value)
        g_printerr ("done.\n");
      else
        g_printerr ("%u%%", 100 * value / n_values);
    }
}

struct _CodePoint
{
  char *line;
  GArray *context_indices;
};

static const char *dir_name = NULL;
static GOptionEntry op_entries[] = {
  { "dir", 'd', 0, G_OPTION_ARG_STRING, &dir_name,
    "name of the output directory", "DIR" },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

static int
compare_p_context_by_total_bytes_descending (gconstpointer a, gconstpointer b)
{
  const Context *ca = * (const Context **) a;
  const Context *cb = * (const Context **) b;
  if (ca->total_bytes < cb->total_bytes)
    return 1;
  if (ca->total_bytes > cb->total_bytes)
    return -1;
  return 0;
}
static int
compare_p_code_point_by_line (gconstpointer a, gconstpointer b)
{
  const CodePoint *ca = * (const CodePoint **) a;
  const CodePoint *cb = * (const CodePoint **) b;
  return strcmp (ca->line, cb->line);
}

static void
pr_context (FILE *fp, guint i, Context *context, const char *image_dir)
{
  fprintf (fp,
           "<p>Context <b>%u</b>:\n"
           "<pre>\n"
           "%s"
           "</pre>\n"
           "<br/>\n"
           "<img src=\"%s/context-%05u.png\" />\n"
           "</p>\n",
           i,
           context->str,
           image_dir,
           i);
}
static gboolean
is_preamble_line (const char *str)
{
  return g_str_has_prefix (str, "rusage:");
}

int main(int argc, char **argv)
{
  unsigned i;
  GError *error = NULL;
  GOptionContext *op_context;
  unsigned line_no = 0;
  GHashTable *contexts_by_str;
  GPtrArray *all_contexts;
  GHashTable *code_points_by_line;
  GPtrArray *all_code_points;
  char *tmp_fname;
  char **files;
  guint n_files;
  AllocInfo *total_by_time;
  op_context = g_option_context_new (NULL);
  g_option_context_set_summary (op_context, "gsk-analyze-successive-memdumps");
  g_option_context_add_main_entries (op_context, op_entries, NULL);
  if (!g_option_context_parse (op_context, &argc, &argv, &error))
    gsk_fatal_user_error ("error parsing command-line options: %s", error->message);
  g_option_context_free (op_context);

  contexts_by_str = g_hash_table_new (g_str_hash, g_str_equal);
  code_points_by_line = g_hash_table_new (g_str_hash, g_str_equal);
  all_contexts = g_ptr_array_new ();
  all_code_points = g_ptr_array_new ();

  if (dir_name == NULL)
    g_error ("missing argument directory");

  n_files = argc - 1;
  files = argv + 1;
  g_printerr ("Scanning %u input files... ", n_files);
  total_by_time = g_new0 (AllocInfo, n_files);
  for (i = 0; i < n_files; i++)
    {
      FILE *fp = fopen (files[i], "r");
      char buf[4096];
      GString *context_str = g_string_new ("");
      AllocInfo ai;
      Context *context;
      update_percent_bar (i, n_files);
      if (fp == NULL)
        gsk_fatal_user_error ("opening %s failed: %s",
	                      files[i], g_strerror (errno));
      do
        {
          if (!fgets (buf, sizeof (buf), fp))
            gsk_fatal_user_error ("file %s: unexpected eof", files[i]);
          line_no++;
        }
      while (is_preamble_line (buf));
next_block_start:
      if (g_str_has_prefix (buf, "Summary: "))
        goto done_file;
      if (sscanf (buf, "%u bytes allocated in %u blocks from:",
                      &ai.n_bytes, &ai.n_blocks) != 2)
        gsk_fatal_user_error ("error parsing line %u from %s",
                              line_no, files[i]);
      g_string_set_size (context_str, 0);
      for (;;)
        {
          if (fgets (buf, sizeof (buf), fp) == NULL)
            gsk_fatal_user_error ("file %s: unexpected eof", files[i]);
          line_no++;
          if (buf[0] != ' ')
            break;
          g_string_append (context_str, buf + 2);
        }

      /* find or create context */
      context = g_hash_table_lookup (contexts_by_str, context_str->str);
      if (context == NULL)
        {
          context = g_new (Context, 1);
          context->str = g_strdup (context_str->str);
          context->alloc_infos = g_new0 (AllocInfo, n_files);
          g_hash_table_insert (contexts_by_str, context->str, context);
          context->total_bytes = 0;
          g_ptr_array_add (all_contexts, context);
        }
      context->alloc_infos[i] = ai;
      context->total_bytes += ai.n_bytes;
      total_by_time[i].n_bytes += ai.n_bytes;
      total_by_time[i].n_blocks += ai.n_blocks;
      goto next_block_start;

done_file:
      fclose (fp);
    }
  update_percent_bar (i, n_files);

  g_ptr_array_sort (all_contexts, compare_p_context_by_total_bytes_descending);
  if (!gsk_mkdir_p (dir_name, 0755, &error))
    gsk_fatal_user_error ("error making directory %s: %s", dir_name, error->message);
  static const char *subdirs[] = { "data", "images", "code-points" };
  for (i = 0; i < G_N_ELEMENTS (subdirs); i++)
    {
      tmp_fname = g_strdup_printf ("%s/%s", dir_name, subdirs[i]);
      if (!gsk_mkdir_p (tmp_fname, 0755, &error))
        g_error ("error mkdir(%s): %s", tmp_fname, error->message);
      g_free (tmp_fname);
    }
  FILE *gnuplot_script_fp, *index_html_fp, *main_html_fp;
  tmp_fname = g_strdup_printf ("%s/gnuplot.input", dir_name);
  gnuplot_script_fp = fopen (tmp_fname, "w");
  if (gnuplot_script_fp == NULL)
    g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
  g_free (tmp_fname);
  fprintf (gnuplot_script_fp,
           "set terminal png\n\n");
  index_html_fp = NULL;

  {
    FILE *fp;
    tmp_fname = g_strdup_printf ("%s/data/total.data", dir_name);
    fp = fopen (tmp_fname, "w");
    if (fp == NULL)
      g_error ("creating %s failed", tmp_fname);
    g_free (tmp_fname);
    for (i = 0; i < n_files; i++)
      fprintf (fp, "%u %u %u\n", i,
               total_by_time[i].n_bytes,
               total_by_time[i].n_blocks);
    fclose (fp);

    fprintf (gnuplot_script_fp,
             "set output \"%s/images/total.png\"\n", dir_name);
    fprintf (gnuplot_script_fp,
             "plot \"%s/data/total.data\" using 1:2 title \"bytes\", \"%s/data/total.data\" using 1:3 title \"blocks\"\n",
             dir_name, dir_name);
  }

  tmp_fname = g_strdup_printf ("%s/index.html", dir_name);
  main_html_fp = fopen (tmp_fname, "w");
  if (main_html_fp == NULL)
    g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
  fprintf (main_html_fp, "<html><body>\n"
                         "Total:\n<br/><img src=\"images/total.png\" /><br/>\n"
                         "<ul>\n");

  g_printerr ("Writing data files for %u contexts... ", all_contexts->len);
  for (i = 0; i < all_contexts->len; i++)
    {
      FILE *fp;
      Context *context = all_contexts->pdata[i];
      guint j;
      update_percent_bar (i, all_contexts->len);
      if (i % 100 == 0)
        {
          if (index_html_fp != NULL)
            {
              fprintf (index_html_fp, "</body></html>\n");
              fclose (index_html_fp);
            }
          tmp_fname = g_strdup_printf ("%s/index-%03u.html", dir_name, i / 100);
          index_html_fp = fopen (tmp_fname, "w");
          if (index_html_fp == NULL)
            g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
          g_free (tmp_fname);
          fprintf (index_html_fp, "<html><body>\n");
          fprintf (index_html_fp, "<h1>Contexts %u .. %u</h1>\n", i, MIN (i + 99, all_contexts->len - 1));

          fprintf (main_html_fp, "<li><a href=\"index-%03u.html\">Contexts %u .. %u</a></li>\n",
                   i / 100,  i, MIN (i + 99, all_contexts->len - 1));
        }

      tmp_fname = g_strdup_printf ("%s/data/context-%05u.data", dir_name, i);
      fp = fopen (tmp_fname, "w");
      if (fp == NULL)
        g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
      for (j = 0; j < n_files; j++)
        fprintf (fp, "%u %u %u\n", j,
                 context->alloc_infos[j].n_bytes,
                 context->alloc_infos[j].n_blocks);
      fclose (fp);
      fprintf (gnuplot_script_fp,
               "set output \"%s/images/context-%05u.png\"\n", dir_name, i);
      fprintf (gnuplot_script_fp,
               "plot \"%s/data/context-%05u.data\" using 1:2 title \"bytes\", \"%s/data/context-%05u.data\" using 1:3 title \"blocks\"\n",
               dir_name, i,
               dir_name, i);

      pr_context (index_html_fp, i, context, "images");
    }
  update_percent_bar (i, all_contexts->len);
  fprintf (main_html_fp, "</ul>\n"
                         "<h1>Code Point Index</h1>\n"
                          "<a href=\"index-by-code-point.html\">here</a>\n");
  if (index_html_fp != NULL)
    {
      fprintf (index_html_fp, "</body></html>\n");
      fclose (index_html_fp);
    }
  fprintf (main_html_fp, "</body></html>\n");
  fclose (main_html_fp);

  g_printerr ("Calculating code-points... ");
  for (i = 0; i < all_contexts->len; i++)
    {
      Context *context = all_contexts->pdata[i];
      CodePoint *cp;
      char **strs = g_strsplit (context->str, "\n", 0);
      unsigned j;
      for (j = 0; strs[j] != NULL; j++)
        {
          g_strstrip (strs[j]);
          if (strs[j][0] != 0)
            {
              cp = g_hash_table_lookup (code_points_by_line, strs[j]);
              if (cp == NULL)
                {
                  cp = g_new (CodePoint, 1);
                  cp->line = g_strdup (strs[j]);
                  cp->context_indices = g_array_new (FALSE, FALSE, sizeof (guint));
                  g_hash_table_insert (code_points_by_line, cp->line, cp);
                  g_ptr_array_add (all_code_points, cp);
                }
              if (cp->context_indices->len == 0
               || g_array_index (cp->context_indices, guint, cp->context_indices->len - 1) != i)
                g_array_append_val (cp->context_indices, i);
            }
        }
      g_strfreev (strs);
    }
  g_printerr (" done [%u code points].\n", all_code_points->len);
  g_ptr_array_sort (all_code_points, compare_p_code_point_by_line);
  tmp_fname = g_strdup_printf ("%s/index-by-code-point.html", dir_name);
  main_html_fp = fopen (tmp_fname, "w");
  if (main_html_fp == NULL)
    g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
  g_free (tmp_fname);
  fprintf (main_html_fp,
           "<html><body><h1>Code Points</h1>\n");
  fprintf (main_html_fp,
           "<ul>\n");
  AllocInfo *totals;
  totals = g_new (AllocInfo, n_files);
  g_printerr ("Creating code-point data... ");
  for (i = 0; i < all_code_points->len; i++)
    {
      CodePoint *cp = all_code_points->pdata[i];
      FILE *fp;
      unsigned j;
      update_percent_bar (i, all_code_points->len);
      memset (totals, 0, sizeof (AllocInfo) * n_files);
      for (j = 0; j < cp->context_indices->len; j++)
        {
          guint context_index = g_array_index (cp->context_indices, guint, j);
          Context *context = all_contexts->pdata[context_index];
          guint k;
          for (k = 0; k < n_files; k++)
            {
              totals[k].n_bytes += context->alloc_infos[k].n_bytes;
              totals[k].n_blocks += context->alloc_infos[k].n_blocks;
            }
        }
      tmp_fname = g_strdup_printf ("%s/data/codepoint-%05u.data", dir_name, i);
      fp = fopen (tmp_fname, "w");
      for (j = 0; j < n_files; j++)
        fprintf (fp, "%u %u %u\n", j,
                 totals[j].n_bytes,
                 totals[j].n_blocks);
      fclose (fp);

      fprintf (gnuplot_script_fp,
               "set output \"%s/images/codepoint-%05u.png\"\n", dir_name, i);
      fprintf (gnuplot_script_fp,
               "plot \"%s/data/codepoint-%05u.data\" using 1:2 title \"bytes\", \"%s/data/codepoint-%05u.data\" using 1:3 title \"blocks\"\n",
               dir_name, i,
               dir_name, i);

      fprintf (main_html_fp, "<li>Code point %05u: <a href=\"code-points/%05u.html\">%s</a> (%u contexts)</li>\n",
               i, i, cp->line, cp->context_indices->len);
      tmp_fname = g_strdup_printf ("%s/code-points/%05u.html", dir_name, i);
      fp = fopen (tmp_fname, "w");
      if (fp == NULL)
        g_error ("error creating %s: %s", tmp_fname, g_strerror (errno));
      g_free (tmp_fname);



      fprintf (fp, "<html><body><h1>Code Point %u</h1>\n"
               "<b><pre>\n"
               "%s\n"
               "</pre>\n"
               "</b>\n", i, cp->line);
      fprintf (fp, "<p>Summary<br /><img src=\"../images/codepoint-%05u.png\" /></p>\n", i);
      for (j = 0; j < MIN (cp->context_indices->len, 100); j++)
        pr_context (fp, g_array_index (cp->context_indices, guint, j),
                    all_contexts->pdata[g_array_index (cp->context_indices, guint, j)],
                    "../images");
      if (j < cp->context_indices->len)
        fprintf (fp, "<p><b>%u Contexts omitted</b></p>\n",
                 (guint)(cp->context_indices->len - j));
      fprintf (fp,
               "</body></html>\n");
      fclose (fp);
    }
  update_percent_bar (i, all_code_points->len);
  fprintf (main_html_fp,
           "</ul>\n"
           "</body></html>\n");
  fclose (main_html_fp);

  fclose (gnuplot_script_fp);

  g_printerr ("Running gnuplot... ");
  tmp_fname = g_strdup_printf ("gnuplot < %s/gnuplot.input", dir_name);
  if (system (tmp_fname) != 0)
    gsk_fatal_user_error ("error running gnuplot");
  g_free (tmp_fname);
  g_printerr (" done.\n");

  return 0;
}
