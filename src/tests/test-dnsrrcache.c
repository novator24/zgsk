#include "../gskdns.h"
#include <time.h>
#include <string.h>

int main (int argc, char **argv)
{
  GskDnsRRCache *rr_cache;
  GskDnsMessage *allocator;
  GskDnsResourceRecord *rr, *found;
  GskDnsResourceRecord *copy;
  GskSocketAddressIpv4 *addr;
  gulong cur_time = time(NULL);
  char *err_msg = NULL;
  guint8 one_two_three_four[4] = {1,2,3,4};
  gsk_init_without_threads (&argc, &argv);

  rr_cache = gsk_dns_rr_cache_new (1024 * 1024, 1024);

  allocator = gsk_dns_message_new (0, FALSE);
  rr = gsk_dns_rr_new_a ("foo.bar", 1000, one_two_three_four, allocator);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
  found = gsk_dns_rr_cache_lookup_one (rr_cache, "foo.bar", GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET,
                                       GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES);
  g_assert (found != NULL);
  g_assert (found->type == GSK_DNS_RR_HOST_ADDRESS);
  g_assert (found->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (memcmp (found->rdata.a.ip_address, one_two_three_four, 4) == 0);
  copy = gsk_dns_rr_copy (found, NULL);
  g_assert (copy != NULL);
  g_assert (copy->type == GSK_DNS_RR_HOST_ADDRESS);
  g_assert (copy->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (memcmp (copy->rdata.a.ip_address, one_two_three_four, 4) == 0);
  gsk_dns_rr_free (copy);

  g_assert (gsk_dns_rr_cache_get_addr (rr_cache, "foo.bar", &addr));
  g_assert (addr != NULL);
  g_assert (memcmp (addr->ip_address, one_two_three_four, 4) == 0);
  g_object_unref (addr);
  addr = NULL;
  gsk_dns_message_unref (allocator);

  allocator = gsk_dns_message_new (0, FALSE);
  rr = gsk_dns_rr_new_cname ("foo.baz", 1000, "foo.bar", allocator);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
  found = gsk_dns_rr_cache_lookup_one (rr_cache, "foo.baz", GSK_DNS_RR_CANONICAL_NAME, GSK_DNS_CLASS_INTERNET,
                                       GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES);
  g_assert (found != NULL);
  g_assert (found->type == GSK_DNS_RR_CANONICAL_NAME);
  g_assert (found->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (strcmp (found->rdata.domain_name, "foo.bar") == 0);
  copy = gsk_dns_rr_copy (found, NULL);
  g_assert (copy != NULL);
  g_assert (copy->type == GSK_DNS_RR_CANONICAL_NAME);
  g_assert (copy->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (strcmp (copy->rdata.domain_name, "foo.bar") == 0);
  gsk_dns_rr_free (copy);
  found = gsk_dns_rr_cache_lookup_one (rr_cache, "foo.baz", GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET,
                                       GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES);
  g_assert (found != NULL);
  g_assert (found->type == GSK_DNS_RR_HOST_ADDRESS);
  g_assert (found->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (memcmp (found->rdata.a.ip_address, one_two_three_four, 4) == 0);
  g_assert (gsk_dns_rr_cache_get_addr (rr_cache, "foo.baz", &addr));
  g_assert (addr != NULL);
  g_assert (memcmp (addr->ip_address, one_two_three_four, 4) == 0);
  g_object_unref (addr);
  addr = NULL;
  gsk_dns_message_unref (allocator);

  allocator = gsk_dns_message_new (0, FALSE);
  rr = gsk_dns_rr_new_hinfo ("foo.bar", 1000, "pentium", "linux", allocator);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
  found = gsk_dns_rr_cache_lookup_one (rr_cache, "foo.baz", GSK_DNS_RR_HOST_INFO, GSK_DNS_CLASS_INTERNET,
                                       GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES);
  g_assert (found != NULL);
  g_assert (found->type == GSK_DNS_RR_HOST_INFO);
  g_assert (found->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (strcmp (found->rdata.hinfo.os, "linux") == 0);
  g_assert (strcmp (found->rdata.hinfo.cpu, "pentium") == 0);
  gsk_dns_message_unref (allocator);

  gsk_dns_rr_cache_unref (rr_cache);

#define ORIGIN ""
  rr_cache = gsk_dns_rr_cache_new (1024 * 1024, 1024);
  allocator = gsk_dns_message_new (0, FALSE);
  rr = gsk_dns_rr_text_parse ("fun.house 10000 IN CNAME extra.fun", NULL, ORIGIN, &err_msg, allocator);
  if (!rr)
    g_message ("error parsing CNAME: %s", err_msg);
  g_assert (rr != NULL);
  g_assert (rr->type == GSK_DNS_RR_CANONICAL_NAME);
  g_assert (rr->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (g_ascii_strcasecmp (rr->rdata.domain_name, "extra.fun.") == 0);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
  rr = gsk_dns_rr_text_parse ("extra.fun 10000 IN A 2.3.4.5", NULL, ORIGIN, &err_msg, allocator);
  if (!rr)
    g_message ("error parsing A: %s", err_msg);
  g_assert (rr != NULL);
  g_assert (rr->type == GSK_DNS_RR_HOST_ADDRESS);
  g_assert (rr->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (g_ascii_strcasecmp (rr->owner, "extra.fun.") == 0);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
#if 0
  rr = gsk_dns_rr_text_parse ("      10000 IN HINFO cpu os", "extra.fun", ORIGIN, &err_msg, allocator);
  if (!rr)
    g_message ("error parsing HINFO: %s", err_msg);
  g_assert (rr != NULL);
  g_assert (rr->type == GSK_DNS_RR_HOST_INFO);
  g_assert (rr->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (g_ascii_strcasecmp (rr->owner, "extra.fun.") == 0);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
#endif
  rr = gsk_dns_rr_text_parse ("       10000 IN MX 10 mail.host", "extra.fun.", ORIGIN, &err_msg, allocator);
  if (!rr)
    g_message ("error parsing MX: %s", err_msg);
  g_assert (rr != NULL);
  g_assert (rr->type == GSK_DNS_RR_MAIL_EXCHANGE);
  g_assert (rr->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (g_ascii_strcasecmp (rr->owner, "extra.fun.") == 0);
  g_assert (g_ascii_strcasecmp (rr->rdata.mx.mail_exchange_host_name, "mail.host.") == 0);
  g_assert (rr->rdata.mx.preference_value == 10);
  gsk_dns_rr_cache_insert (rr_cache,rr,TRUE,cur_time);
  copy = gsk_dns_rr_copy (rr, NULL);
  g_assert (copy->type == GSK_DNS_RR_MAIL_EXCHANGE);
  g_assert (copy->record_class == GSK_DNS_CLASS_INTERNET);
  g_assert (g_ascii_strcasecmp (copy->owner, "extra.fun.") == 0);
  g_assert (g_ascii_strcasecmp (copy->rdata.mx.mail_exchange_host_name, "mail.host.") == 0);
  g_assert (copy->rdata.mx.preference_value == 10);
  gsk_dns_rr_free (copy);

  gsk_dns_message_unref (allocator);

  g_assert (gsk_dns_rr_cache_lookup_one (rr_cache, "negative.nelly",
					 GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET,
                                         GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES) == NULL);
  g_assert (!gsk_dns_rr_cache_is_negative (rr_cache, "negative.nelly",
					   GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET));
  gsk_dns_rr_cache_add_negative (rr_cache, "negative.nelly",
				 GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET,
				 cur_time + 1000, TRUE);
  g_assert (gsk_dns_rr_cache_lookup_one (rr_cache, "negative.nelly",
					 GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET,
                                         GSK_DNS_RR_CACHE_LOOKUP_DEREF_CNAMES) == NULL);
  g_assert (gsk_dns_rr_cache_is_negative (rr_cache, "negative.nelly",
					  GSK_DNS_RR_HOST_ADDRESS, GSK_DNS_CLASS_INTERNET));

  gsk_dns_rr_cache_unref (rr_cache);


  return 0;
}
