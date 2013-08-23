/*
    GSKB - a batch processing framework

    gskb-uint-table-internals:  internal implementation details of our uint -> any mapping.

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
  GSKB_UINT_TABLE_EMPTY,
  GSKB_UINT_TABLE_DIRECT,
  GSKB_UINT_TABLE_BSEARCH,
  GSKB_UINT_TABLE_RANGES
  //GSKB_UINT_TABLE_HASH_DIRECT
} GskbUIntTableType;

typedef struct _GskbUIntTableRange GskbUIntTableRange;
struct _GskbUIntTableRange
{
  guint32 start, count;
  guint entry_data_offset;
};
