#include <glib.h>

typedef struct _GskIndexer GskIndexer;
typedef struct _GskIndexerReader GskIndexerReader;

typedef int (*GskIndexerCompareFunc) (unsigned        a_len,
                                      const guint8   *a_data,
                                      unsigned        b_len,
                                      const guint8   *b_data,
				      void           *user_data);

typedef enum
{
  GSK_INDEXER_MERGE_RETURN_A,
  GSK_INDEXER_MERGE_RETURN_B,
  GSK_INDEXER_MERGE_IN_PAD,
  GSK_INDEXER_MERGE_DISCARD
} GskIndexerMergeResult;

typedef GskIndexerMergeResult (*GskIndexerMergeFunc)(unsigned        a_len,
                                                     const guint8   *a_data,
                                                     unsigned        b_len,
                                                     const guint8   *b_data,
				                     GByteArray     *pad,
				                     void           *user_data);


GskIndexer       *gsk_indexer_new         (GskIndexerCompareFunc compare,
                                           GskIndexerMergeFunc   merge,
			                   void                 *user_data);
void              gsk_indexer_add         (GskIndexer           *indexer,
                                           unsigned              length,
					   const guint8         *data);
GskIndexerReader *gsk_indexer_make_reader (GskIndexer           *indexer);
void              gsk_indexer_destroy     (GskIndexer           *indexer);

gboolean      gsk_indexer_reader_has_data (GskIndexerReader *reader);
gboolean      gsk_indexer_reader_peek_data(GskIndexerReader *reader,
                                           unsigned         *len_out,
                                           const guint8    **data_out);
gboolean      gsk_indexer_reader_advance  (GskIndexerReader *reader);
void          gsk_indexer_reader_destroy  (GskIndexerReader *reader);


