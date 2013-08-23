/*
    GSKB - a batch processing framework

    gskb-str-table-internals:  internal implementation details of the str-table code.

    Copyright (C) 2008 Dave Benson

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


typedef enum
{
  GSKB_STR_TABLE_BSEARCH,
  GSKB_STR_TABLE_ABLZ,		/* hash-table */
  GSKB_STR_TABLE_5003_33        /* hash-table */
} GskbStrTableType;

typedef struct _GskbStrTableBSearchEntry GskbStrTableBSearchEntry;
struct _GskbStrTableBSearchEntry
{
  guint32 str_slab_offset;
  /* data follows */
};
#define GSKB_STR_TABLE_BSEARCH_ENTRY__ALIGNOF  GSKB_ALIGNOF_UINT32

typedef struct _GskbStrTableHashEntry GskbStrTableHashEntry;
struct _GskbStrTableHashEntry
{
  guint32 hash_code;
  guint32 str_slab_offset;
  /* data follows */
};
#define GSKB_STR_TABLE_HASH_ENTRY__ALIGNOF  GSKB_ALIGNOF_UINT32
