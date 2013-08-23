/*
    GSK - a library to write servers
    Copyright (C) 1999-2000 Dave Benson

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

#include <sys/stat.h>
#include <stdio.h>
#include "gskfilecache.h"

/* --- prototypes --- */
static GtkObjectClass *parent_class = NULL;

/* --- cache entry --- */
typedef struct _CacheEntry CacheEntry;
struct _CacheEntry
{
  char *filename;
  int length;			/* if length == -1, file-not-found */
  char *contents;
};


/* --- cache entry implementation --- */
static CacheEntry *
cache_entry_new (const char *filename)
{
  struct stat stat_buf;
  CacheEntry *rv = g_new (CacheEntry, 1);
  rv->filename = g_strdup (filename);
  rv->contents = NULL;
  if (stat (filename, &stat_buf) < 0
   || !S_ISREG (stat_buf.st_mode))
    {
      rv->length = -1;
    }
  else
    {
      FILE *fp = fopen (filename, "r");
      rv->length =stat_buf.st_size;
      rv->contents = g_new (char, rv->length);
      if (fp == NULL
       || fread (rv->contents, rv->length, 1, fp) != 1)
        {
          rv->length = -1;
          g_free (rv->contents);
          rv->contents = NULL;
        }
      fclose (fp);
    }
  return rv;
}

static void
cache_entry_delete (CacheEntry *entry)
{
  g_free (entry->contents);
  g_free (entry->filename);
  g_free (entry);
}

/* --- private methods --- */
static CacheEntry *
gsk_file_cache_get (GskFileCache *cache,
                    const char   *filename)
{
  CacheEntry *entry;
  entry = g_hash_table_lookup (cache->cache, filename);
  if (entry == NULL)
    {
      entry = cache_entry_new (filename);
      g_hash_table_insert (cache->cache, entry->filename, entry);
    }
  return entry;
}

/* --- public methods --- */
gboolean           gsk_file_cache_test_file    (GskFileCache     *cache,
                                                const char       *path)
{
  CacheEntry *entry;
  entry = gsk_file_cache_get (cache, path);
  return entry->length >= 0;
}

void               gsk_file_cache_get_file_size(GskFileCache     *cache,
                                                const char       *path,
					        int              *size_out)
{
  CacheEntry *entry;
  entry = gsk_file_cache_get (cache, path);
  *size_out = entry->length;
}
  
gconstpointer      gsk_file_cache_get_content  (GskFileCache     *cache,
                                                const char       *path)
{
  CacheEntry *entry;
  entry = gsk_file_cache_get (cache, path);
  return entry->contents;
}

void               gsk_file_cache_release      (GskFileCache     *cache,
                                                gconstpointer     data)
{
  /* XXX: we don't release memory til we are destroyed.
          we *should* ref count or something... */
  (void) cache;
  (void) data;
}
/* --- constructors --- */
GskFileCache      *gsk_file_cache_new          ()
{
  return GSK_FILE_CACHE (gsk_gtk_object_new (GSK_TYPE_FILE_CACHE));
}

GskFileCache      *gsk_file_cache_get_global   ()
{
  static GskFileCache *global_cache = NULL;
  if (global_cache == NULL)
    global_cache = gsk_file_cache_new ();
  gtk_object_ref (GTK_OBJECT (global_cache));
  return global_cache;
}

/* --- GtkObject methods --- */
static void free_cache_entry(gpointer k, gpointer value, gpointer unused)
{
  (void) unused;
  (void) k;
  cache_entry_delete ((CacheEntry *)value);
}
static void
gsk_file_cache_finalize (GtkObject *object)
{
  GskFileCache *cache = GSK_FILE_CACHE (object);
  g_hash_table_foreach (cache->cache, free_cache_entry, NULL);
  g_hash_table_destroy (cache->cache);
  (*parent_class->finalize) (object);
}

/* --- functions --- */
static void
gsk_file_cache_init (GskFileCache *file_cache)
{
  file_cache->cache = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
gsk_file_cache_class_init (GtkObjectClass *class)
{
  class->finalize = gsk_file_cache_finalize;
}

GtkType gsk_file_cache_get_type()
{
  static GtkType file_cache_type = 0;
  if (!file_cache_type)
    {
      static const GtkTypeInfo file_cache_info =
      {
	"GskFileCache",
	sizeof(GskFileCache),
	sizeof(GskFileCacheClass),
	(GtkClassInitFunc) gsk_file_cache_class_init,
	(GtkObjectInitFunc) gsk_file_cache_init,
	/* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL
      };
      GtkType parent = GTK_TYPE_OBJECT;
      file_cache_type = gtk_type_unique (parent, &file_cache_info);
      parent_class = gtk_type_class (parent);
    }
  return file_cache_type;
}
