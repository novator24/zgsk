#include "gsksimpleacl.h"

typedef struct _GskSimpleAclRule GskSimpleAclRule;

/* --- fast acl allocation & manipulation --- */
struct _GskSimpleAclRule
{
  guint8            is_subacl : 1;
  guint8            is_accept : 1;

  guint8            ip_address[4];
  guint8            significant_bits;

  GskSimpleAcl     *sub_acl;
  GskSimpleAclRule *prev;
  GskSimpleAclRule *next;
};

struct _GskSimpleAcl
{
  GskSimpleAclRule *first;
  GskSimpleAclRule *last;
  gboolean          accept_by_default;
};

/* Allocate a slab of memory big enough for a 
   GskSimpleAcl or a GskSimpleAclRule. */
static GMemChunk *simple_acl_chunk = NULL;
static gpointer acl_alloc ()
{
  if (simple_acl_chunk == NULL)
    simple_acl_chunk = g_mem_chunk_new ("Acl Mem Chunk",
					MAX (sizeof (GskSimpleAclRule),
					     sizeof (GskSimpleAcl)),
					1024,
					G_ALLOC_AND_FREE);
  return g_mem_chunk_alloc (simple_acl_chunk);
}
static void acl_free (gpointer ptr)
{
  g_mem_chunk_free (simple_acl_chunk, ptr);
}

/* --- public interface --- */
GskSimpleAcl *
gsk_simple_acl_new (gboolean accept_by_default)
{
  GskSimpleAcl *acl = acl_alloc ();
  acl->first = acl->last = NULL;
  acl->accept_by_default = accept_by_default;
  gsk_debug_track (__FILE__, __LINE__, acl, 
		   "GskSimpleAcl", "gsk_simple_acl_destroy");
  return acl;
}

static GskSimpleAclRule *
acl_rule_new (const guint8  *ip_addr,
	      guint          num_bits,
	      gboolean       accept)
{
  GskSimpleAclRule *rv;
  rv = acl_alloc ();
  rv->is_subacl = 0;
  rv->is_accept = accept ? 1 : 0;
  memcpy (rv->ip_address, ip_addr, 4);
  rv->significant_bits = num_bits;
  rv->next = rv->prev = NULL;
  rv->sub_acl = NULL;
  return rv;
}
  
static GskSimpleAclRule *
acl_rule_new_sub (GskSimpleAcl   *subrule,
		  gboolean        accept)
{
  GskSimpleAclRule *rv;
  rv = acl_alloc ();
  rv->is_subacl = 1;
  rv->is_accept = accept ? 1 : 0;
  rv->significant_bits = 0;
  rv->next = rv->prev = NULL;
  rv->sub_acl = subrule;
  return rv;
}
  
static inline void
append_generic(GskSimpleAcl     *acl,
	       GskSimpleAclRule *rule)
{
  rule->prev = acl->last;
  rule->next = NULL;

  if (acl->last != NULL)
    acl->last->next = rule;
  else
    acl->first = rule;
  acl->last = rule;
}

static inline void
prepend_generic(GskSimpleAcl   *acl,
		GskSimpleAclRule *rule)
{
  rule->prev = NULL;
  rule->next = acl->first;

  if (acl->first!= NULL)
    acl->first->prev = rule;
  else
    acl->last = rule;
  acl->first = rule;
}

void
gsk_simple_acl_append_accept (GskSimpleAcl   *acl,
			      const guint8   *ip_address,
			      guint           significant_bits)
{
  append_generic (acl, acl_rule_new (ip_address, significant_bits, TRUE));
}

void
gsk_simple_acl_prepend_accept(GskSimpleAcl   *acl,
			      const guint8   *ip_address,
			      guint           significant_bits)
{
  prepend_generic (acl, acl_rule_new (ip_address, significant_bits, TRUE));
}

void
gsk_simple_acl_append_reject (GskSimpleAcl   *acl,
			      const guint8   *ip_address,
			      guint           significant_bits)
{
  append_generic (acl, acl_rule_new (ip_address, significant_bits, FALSE));
}

void
gsk_simple_acl_prepend_reject(GskSimpleAcl   *acl,
			      const guint8   *ip_address,
			      guint           significant_bits)
{
  prepend_generic (acl, acl_rule_new (ip_address, significant_bits, FALSE));
}

void
gsk_simple_acl_prepend_sub    (GskSimpleAcl   *acl,
			       gboolean        accept_if_matched,
			       GskSimpleAcl   *sub_acl)
{
  prepend_generic (acl, acl_rule_new_sub (sub_acl, accept_if_matched));
}

void
gsk_simple_acl_append_sub   (GskSimpleAcl   *acl,
			     gboolean        accept_if_matched,
			     GskSimpleAcl   *sub_acl)
{
  append_generic (acl, acl_rule_new_sub (sub_acl, accept_if_matched));
}

void
gsk_simple_acl_set_default   (GskSimpleAcl   *acl,
			      gboolean        accept_by_default)
{
  acl->accept_by_default = accept_by_default;
}

void
gsk_simple_acl_destroy       (GskSimpleAcl   *simple_acl)
{
  GskSimpleAclRule *rule = simple_acl->first;
  while (rule != NULL)
    {
      GskSimpleAclRule* next = rule->next;
      acl_free (rule);
      rule = next;
    }
  acl_free (simple_acl);
}

static inline gboolean
bit_match (const guint8 *addr1, const guint8 *addr2, guint bits)
{
  if (bits >= 8 && memcmp (addr1, addr2, bits / 8) != 0)
    return FALSE;
  if (bits % 8 != 0)
    {
      guint8 mask = 255 << (7 - bits % 8);
      if ((addr1[bits/8] & mask) != (addr2[bits/8] & mask))
	return FALSE;
    }
  return TRUE;
}

gboolean
gsk_simple_acl_check         (GskSimpleAcl   *simple_acl,
			      const guint8   *ip_address)
{
  GskSimpleAclRule *rule;
  for (rule = simple_acl->first; rule != NULL; rule = rule->next)
    if (rule->is_subacl)
      {
        if (gsk_simple_acl_check (rule->sub_acl, ip_address))
	  return rule->is_accept;
      }
    else
      {
	if (bit_match (ip_address, rule->ip_address, rule->significant_bits))
	  return rule->is_accept;
      }
  return simple_acl->accept_by_default;
}
