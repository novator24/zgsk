#include "../gsknetworkinterface.h"
#include "../gskdebug.h"
#include "../gskinit.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void
usage ()
{
  fprintf (stderr, "usage: %s FLAGS\n\n"
                   "List the network interfaces, subject to the constraints\n"
		   "specified in FLAGS; FLAGS is any number of the\n"
		   "following characters concatenated:\n"
		   "   u     Only list interfaces that are up.\n"
		   "   l     Only list loopback interfaces\n"
		   "   r     Only list nonloopback interfaces\n"
		   "   b     Only list interfaces which have a broadcast addr\n"
		   "(If FLAGS is an empty string, then all interfaces are\n"
		   "printed, use '' to indicate an empty string to the shell.)\n"
		   , g_get_prgname());
  exit (1);
}

static void
print_address (const char *comment, GskSocketAddress *address)
{
  char *str = gsk_socket_address_to_string (address);
  printf ("\t%s:\t%s\n", comment, str);
  g_free (str);
}

int main(int argc, char ** argv)
{
  guint flags = 0;
  GskNetworkInterfaceSet *iset;
  guint i;

  gsk_init_without_threads (&argc, &argv);

  if (argc != 2 || argv[0][1] == '-')
    usage ();

  if (strchr (argv[1], 'u'))
    flags |= GSK_NETWORK_INTERFACE_UP;
  if (strchr (argv[1], 'l'))
    flags |= GSK_NETWORK_INTERFACE_LOOPBACK;
  if (strchr (argv[1], 'r'))
    flags |= GSK_NETWORK_INTERFACE_NON_LOOPBACK;
  if (strchr (argv[1], 'b'))
    flags |= GSK_NETWORK_INTERFACE_HAS_BROADCAST;

  iset = gsk_network_interface_set_new (flags);
  g_assert (iset != NULL);

  for (i = 0; i < iset->num_interfaces; i++)
    {
      GskNetworkInterface *iface = iset->interfaces + i;
      printf ("Interface %d: %s\n", i, iface->ifname);

      if (iface->address != NULL)
	print_address ("IP Address", iface->address);
      if (iface->hw_address != NULL)
	print_address ("HW Address", iface->hw_address);
      if (iface->p2p_address != NULL)
	print_address ("P2P Address", iface->p2p_address);
      if (iface->broadcast != NULL)
	print_address ("Broadcast Addr", iface->broadcast);
      printf ("\tHas multicast?\t%d\n", iface->supports_multicast);
      printf ("\tIs promiscuous?\t%d\n", iface->is_promiscuous);
    }

  gsk_network_interface_set_destroy (iset);

  return 0;
}
