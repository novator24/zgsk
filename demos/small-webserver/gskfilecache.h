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

#ifndef __GSK_FILE_CACHE_H_
#define __GSK_FILE_CACHE_H_

#include <gsk/gskgtk.h>

G_BEGIN_DECLS

/* --- typedefs --- */
typedef struct _GskFileCache GskFileCache;
typedef struct _GskFileCacheClass GskFileCacheClass;

/* --- type macros --- */
GtkType gsk_file_cache_get_type();
#define GSK_TYPE_FILE_CACHE			(gsk_file_cache_get_type ())
#define GSK_FILE_CACHE(obj)              (GTK_CHECK_CAST ((obj), GSK_TYPE_FILE_CACHE, GskFileCache))
#define GSK_FILE_CACHE_CLASS(klass)      (GTK_CHECK_CLASS_CAST ((klass), GSK_TYPE_FILE_CACHE, GskFileCacheClass))
#define GSK_FILE_CACHE_GET_CLASS(obj)    (GSK_FILE_CACHE_CLASS(GTK_OBJECT(obj)->klass))
#define GSK_IS_FILE_CACHE(obj)           (GTK_CHECK_TYPE ((obj), GSK_TYPE_FILE_CACHE))
#define GSK_IS_FILE_CACHE_CLASS(klass)   (GTK_CHECK_CLASS_TYPE ((klass), GSK_TYPE_FILE_CACHE))

/* --- structures --- */
struct _GskFileCacheClass 
{
  GtkObjectClass	object_class;
};
struct _GskFileCache 
{
  GtkObject		object;
  GHashTable           *cache;
};

/* --- prototypes --- */
GskFileCache      *gsk_file_cache_get_global   ();
GskFileCache      *gsk_file_cache_new          ();
gboolean           gsk_file_cache_test_file    (GskFileCache     *cache,
                                                const char       *path);
void               gsk_file_cache_get_file_size(GskFileCache     *cache,
                                                const char       *path,
					        int              *size_out);
gconstpointer      gsk_file_cache_get_content  (GskFileCache     *cache,
                                                const char       *path);
void               gsk_file_cache_release      (GskFileCache     *cache,
                                                gconstpointer     data);

G_END_DECLS

#endif
