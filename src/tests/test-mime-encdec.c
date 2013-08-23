#include "../mime/gskmimemultipartencoder.h"
#include "../mime/gskmimemultipartdecoder.h"
#include "../gskmainloop.h"
#include "../gskinit.h"
#include "../gskutils.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#if 1           /* harder version of the test */
#define RANDOMIZE       1
#define N_TESTS		3000
#define MAX_N_PIECES	20
#else           /* easier, repeatable version of the test */
#define RANDOMIZE       0
#define N_TESTS		1
#define MAX_N_PIECES	3
#endif

#define MAX_PIECE_SIZE  100
#define MAX_FIELD_LEN	30

static gboolean
strequal (const char *a, const char *b)
{
  if (a == NULL || b == NULL)
    return a == b;
  return strcmp (a, b) == 0;
}

typedef struct _DecoderInfo DecoderInfo;
struct _DecoderInfo
{
  GQueue *pieces;
  gboolean shutdown;
  GskMimeMultipartDecoder *decoder;
};

static gboolean
handle_new_piece_available (GskMimeMultipartDecoder *decoder,
			    gpointer data)
{
  DecoderInfo *info = data;
  GskMimeMultipartPiece *piece = gsk_mime_multipart_decoder_get_piece (decoder);
  g_assert (info->decoder == decoder);
  if (piece != NULL)
    g_queue_push_tail (info->pieces, piece);
  return TRUE;
}
static gboolean
handle_new_piece_shutdown (GskMimeMultipartDecoder *decoder, gpointer data)
{
  DecoderInfo *info = data;
  g_assert (info->decoder == decoder);
  info->shutdown = TRUE;
  return FALSE;
}

static void
add_piece_to_encoder (gpointer mime_piece, gpointer data)
{
  GskMimeMultipartEncoder *encoder = GSK_MIME_MULTIPART_ENCODER (data);
  GError *error = NULL;
  if (!gsk_mime_multipart_encoder_add_part (encoder, mime_piece, &error))
    g_error ("encoder-add-part: %s", error->message);
}

void
test_with_pieces (GSList *pieces)
{
  GskMimeMultipartEncoder *encoder;
  GskMimeMultipartDecoder *decoder;
  char *options[3];
  DecoderInfo decoder_info;

  /* Assert that the input pieces are memory slabs, not streams.
     
     We only cope with pieces which are memory slabs
     in this test code.  Actually the 'memory' case 
     is a wrapper around the streaming case,
     so testing the streaming case is unlike to turn
     up new bugs. */
  {
    GSList *tmp;
    for (tmp = pieces; tmp != NULL; tmp = tmp->next)
      {
	GskMimeMultipartPiece *piece = tmp->data;
	g_assert (piece->is_memory);
      }
  }

  options[0] = "boundary";
  options[1] = GSK_MIME_MULTIPART_ENCODER_GOOD_BOUNDARY;
  options[2] = NULL;
  decoder = gsk_mime_multipart_decoder_new (options);
  g_assert (decoder);
  gsk_mime_multipart_decoder_set_mode (decoder, GSK_MIME_MULTIPART_DECODER_MODE_ALWAYS_MEMORY);

  encoder = gsk_mime_multipart_encoder_new_defaults ();
  g_assert (encoder);

  g_assert (gsk_stream_attach (GSK_STREAM (encoder),
			       GSK_STREAM (decoder),
			       NULL));
  g_slist_foreach (pieces, add_piece_to_encoder, encoder);

  decoder_info.pieces = g_queue_new ();
  decoder_info.shutdown = FALSE;
  decoder_info.decoder = decoder;
  gsk_mime_multipart_decoder_trap (decoder, handle_new_piece_available,
				   handle_new_piece_shutdown,
				   &decoder_info, NULL);
  gsk_mime_multipart_encoder_terminate (encoder);
  while (!decoder_info.shutdown)
    gsk_main_loop_run (gsk_main_loop_default (), -1, NULL);
  g_assert (g_slist_length (pieces) == g_list_length (decoder_info.pieces->head));

  while (pieces != NULL)
    {
      GskMimeMultipartPiece *orig_piece = pieces->data;
      GskMimeMultipartPiece *out_piece = g_queue_pop_head (decoder_info.pieces);
      pieces = g_slist_remove (pieces, orig_piece);

      /* Compute various aspects of orig_piece/out_piece. */
      if (orig_piece->content_length != out_piece->content_length
       || memcmp (orig_piece->content_data,
		  out_piece->content_data,
		  orig_piece->content_length) != 0)
	{
	  g_message ("orig-piece=%u bytes; new-piece=%u bytes; encoding=%s, id=%s, desc=%s",
		     orig_piece->content_length, out_piece->content_length,
		     orig_piece->transfer_encoding, orig_piece->id,
		     orig_piece->description);
	  g_error ("mismatched content in piece:\n"
		   "  orig='%s'\n"
		   "  got='%s'\n"
		   "  orig encoding=%s\n",
		   gsk_escape_memory (orig_piece->content_data, orig_piece->content_length),
		   gsk_escape_memory (out_piece->content_data, out_piece->content_length),
		   orig_piece->transfer_encoding);
	}
      g_assert (strequal (orig_piece->type, out_piece->type));
      g_assert (strequal (orig_piece->subtype, out_piece->subtype));
      g_assert (strequal (orig_piece->id, out_piece->id));
      g_assert (strequal (orig_piece->description, out_piece->description));
      g_assert (strequal (orig_piece->charset, out_piece->charset));
      g_assert (strequal (orig_piece->location, out_piece->location));
      g_assert (strequal (orig_piece->transfer_encoding, out_piece->transfer_encoding));
      g_assert (strequal (orig_piece->disposition, out_piece->disposition));
      // TODO: test 'other_fields'...

      gsk_mime_multipart_piece_unref (orig_piece);
      gsk_mime_multipart_piece_unref (out_piece);
    }
  g_queue_free (decoder_info.pieces);
  return;
}

const char *rand_chars = G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-";
guint n_rand_chars = 26 + 26 + 10 + 1;

static void
rand_nice_str (char *buf, guint len)
{
  guint i;
  len = g_random_int_range (1, len);
  for (i = 0; i < len; i++)
    buf[i] = rand_chars[g_random_int_range (0, n_rand_chars)];
  buf[len] = 0;
}

static GskMimeMultipartPiece *
make_random_piece (void)
{
  GskMimeMultipartPiece *piece = gsk_mime_multipart_piece_alloc ();
  guint r = rand () / (RAND_MAX / 3 + 1);
  gboolean no_hyphen_prefix = FALSE;
  const char *transfer_enc = NULL;
  switch (r)
    {
    case 0:
      /* Generate a plain-text piece.
	 Such text will never have a 
	 line starting with '-', to guarantee
	 that it's distinguishable from boundary. */
      no_hyphen_prefix = TRUE;
      break;
    case 1:
      /* Generate a "quoted-printable" piece.
	 May contain any binary data. */
      transfer_enc = "quoted-printable";
      break;
    case 2:
      /* Generate a "base64" piece.
	 May contain any binary data. */
      transfer_enc = "base64";
      break;
    }
  gsk_mime_multipart_piece_set_transfer_encoding (piece, transfer_enc);

  /* Generate the random data. */
  {
    guint len = rand() / (RAND_MAX/MAX_PIECE_SIZE) + 1;
    char *slab = g_malloc (len);
    guint i;
    if (no_hyphen_prefix)
      for (i = 0; i < len; i++)
	do {
	  slab[i] = (char) rand ();
	} while (slab[i] == '-');
    else
      for (i = 0; i < len; i++)
	slab[i] = (char) rand ();
    gsk_mime_multipart_piece_set_data (piece, slab, len, g_free, slab);
  }

  /* Set other fields randomly. */
  if (rand () / (RAND_MAX/2))
    {
      char buf0[MAX_FIELD_LEN], buf1[MAX_FIELD_LEN], buf2[MAX_FIELD_LEN];
      char *charset = NULL;
      rand_nice_str (buf0, sizeof (buf0));
      rand_nice_str (buf1, sizeof (buf1));
      if (rand () / (RAND_MAX/2))
	{
	  rand_nice_str (buf2, sizeof (buf2));
	  charset = buf2;
	}
      gsk_mime_multipart_piece_set_type (piece, buf0, buf1, charset, NULL);
    }
  if (rand () / (RAND_MAX/2))
    {
      char buf[256];
      rand_nice_str (buf, sizeof (MAX_FIELD_LEN));
      gsk_mime_multipart_piece_set_id (piece, buf);
    }
  if (rand () / (RAND_MAX/2))
    {
      char buf[256];
      rand_nice_str (buf, sizeof (MAX_FIELD_LEN));
      gsk_mime_multipart_piece_set_description (piece, buf);
    }
  if (rand () / (RAND_MAX/2))
    {
      char buf[256];
      rand_nice_str (buf, sizeof (MAX_FIELD_LEN));
      gsk_mime_multipart_piece_set_location (piece, buf);
    }

  return piece;
}


int
main (int argc, char **argv)
{
  guint test_count;
  gsk_init_without_threads (&argc, &argv);
  
#if RANDOMIZE
  srand (time (0));
#else
  srand(33);
  g_random_set_seed (23);
#endif

  g_printerr ("Running mime loop... ");
  for (test_count = 0; test_count < N_TESTS; test_count++)
    {
      GSList *pieces = NULL;
      guint n_pieces = g_random_int_range (1, MAX_N_PIECES + 1);
      guint piece_count;
      for (piece_count = 0; piece_count < n_pieces; piece_count++)
	pieces = g_slist_prepend (pieces, make_random_piece ());
      if (test_count % 10 == 0)
	g_printerr (".");
      test_with_pieces (pieces);
    }
  g_printerr (" done.\n");
  return 0;
}
