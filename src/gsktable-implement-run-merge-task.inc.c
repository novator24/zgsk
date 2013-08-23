
#if HAS_LEN
# define MAYBE_LEN(len_param) len_param,
#else
# define MAYBE_LEN(len_param)
#endif

static gboolean
MANGLE_FUNC_NAME(copy_file_reader) (GskTable  *table,
                                    MergeTask *task,
                                    guint      input_index,
                                    guint      n_written,
                                    guint      iterations,
                                    gboolean  *is_done_out,
                                    GError   **error)
{
  GskTableReader *reader = task->info.started.inputs[input_index].reader;
  GskTableFile *output = task->info.started.output;
  for (;;)
    {
      n_written++;
      switch (gsk_table_file_feed_entry (output,
                                         reader->key_len, reader->key_data,
                                         reader->value_len, reader->value_data,
                                         error))
        {
        case GSK_TABLE_FEED_ENTRY_WANT_MORE:
          break;
        case GSK_TABLE_FEED_ENTRY_SUCCESS:
          set_buffer (&task->info.started.last_queryable_key,
                      reader->key_len, reader->key_data);
          task->info.started.has_last_queryable_key = TRUE;
#if DO_FLUSH
          if (n_written >= iterations)
            return TRUE;
#endif
          break;
        case GSK_TABLE_FEED_ENTRY_ERROR:
          return FALSE;
        default:
          g_assert_not_reached ();
        }
      n_written++;
#if !DO_FLUSH
      if (n_written >= iterations)
        {
          *is_done_out = FALSE;
          return TRUE;
        }
#endif
      gsk_table_reader_advance (reader);
      if (reader->error != NULL)
        {
          if (error)
            *error = g_error_copy (reader->error);
          return FALSE;
        }
      if (reader->eof)
        {
          *is_done_out = TRUE;
          return TRUE;
        }
    }
}


static gboolean
MANGLE_FUNC_NAME(run_merge_task)  (GskTable      *table,
                                   guint          iterations,
	                           GError       **error)
{
  MergeTask *task = table->run_list;
  guint n_written = 0;
  g_assert (task->is_started);

#if !USE_MEMCMP
# if HAS_LEN
  GskTableCompareFunc compare = table->compare.with_len;
# else
  GskTableCompareFuncNoLen compare = table->compare.no_len;
# endif
#endif

#if HAS_MERGE
# if HAS_LEN
  GskTableMergeFunc merge = table->merge.with_len;
# else
  GskTableMergeFuncNoLen merge = table->merge.no_len;
# endif
#endif

#if DO_SIMPLIFY
# if HAS_LEN
  GskTableSimplifyFunc simplify = table->simplify.with_len;
# else
  GskTableSimplifyFuncNoLen simplify = table->simplify.no_len;
# endif
  GskTableBuffer *simp_buffer = &table->simplify_buffer;
#endif

#if !USE_MEMCMP || DO_SIMPLIFY || HAS_MERGE
  gpointer user_data = table->user_data;
#endif
#if HAS_MERGE
  GskTableBuffer *merge_buf = &table->merge_buffer;
#endif

#if DO_FLUSH
  gboolean is_finished = FALSE;
#endif
  GskTableFile *output = task->info.started.output;

  GskTableReader *readers[2] = { task->info.started.inputs[0].reader,
                                 task->info.started.inputs[1].reader };

restart_testing_eof:

  if (readers[0]->eof)
    {
      gboolean is_done;
      if (readers[1]->eof)
	{
	  goto done;
	}
      if (!MANGLE_FUNC_NAME (copy_file_reader) (table, task, 1,
                                                n_written, iterations,
                                                &is_done, error))
        return FALSE;
      if (is_done)
        goto done;
      return TRUE;
    }
  else if (readers[1]->eof)
    {
      gboolean is_done;
      if (!MANGLE_FUNC_NAME (copy_file_reader) (table, task, 0,
                                                n_written, iterations,
                                                &is_done, error))
        return FALSE;
      if (is_done)
        goto done;
      return TRUE;
    }
  else
    {
      int compare_rv;
restart_with_both_readers:
#if USE_MEMCMP
      compare_rv = compare_memory (readers[0]->key_len,
                                   readers[0]->key_data,
                                   readers[1]->key_len,
                                   readers[1]->key_data);
#else
      compare_rv = compare (MAYBE_LEN (readers[0]->key_len)
                            readers[0]->key_data,
                            MAYBE_LEN (readers[1]->key_len)
                            readers[1]->key_data,
                            user_data);
#endif

#if HAS_MERGE
      if (compare_rv < 0)
#else
      if (compare_rv <= 0)
#endif
	{
	  /* write a */
          guint value_len;
          const guint8 *value_data;
#if DO_SIMPLIFY
	  switch (simplify (MAYBE_LEN (readers[0]->key_len)
                            readers[0]->key_data,
	                    MAYBE_LEN (readers[0]->value_len)
                            readers[0]->value_data,
                            simp_buffer,
                            user_data))
            {
            case GSK_TABLE_SIMPLIFY_IDENTITY:
              value_len = readers[0]->value_len;
              value_data = readers[0]->value_data;
              break;
            case GSK_TABLE_SIMPLIFY_SUCCESS:
              value_len = simp_buffer->len;
              value_data = simp_buffer->data;
              break;
            case GSK_TABLE_SIMPLIFY_DELETE:
              goto do_advance_a;
            default:
              g_assert_not_reached ();
            }
#else
          value_len = readers[0]->value_len;
          value_data = readers[0]->value_data;
#endif
          n_written++;
          switch (gsk_table_file_feed_entry (output,
                                             readers[0]->key_len,
                                             readers[0]->key_data,
                                             value_len,
                                             value_data,
                                             error))
            {
            case GSK_TABLE_FEED_ENTRY_WANT_MORE:
              break;
            case GSK_TABLE_FEED_ENTRY_SUCCESS:
              set_buffer (&task->info.started.last_queryable_key,
                          readers[0]->key_len, readers[0]->key_data);
              task->info.started.has_last_queryable_key = TRUE;
#if DO_FLUSH
              if (n_written >= iterations)
                is_finished = TRUE;
#endif
              break;
            case GSK_TABLE_FEED_ENTRY_ERROR:
              return FALSE;
            default:
              g_assert_not_reached ();
            }

#if DO_SIMPLIFY
do_advance_a:
#endif
          /* advance a, possibly becoming eof or error */
          gsk_table_reader_advance (readers[0]);
          if (readers[0]->error)
            {
              g_set_error (error, readers[0]->error->domain,
                           readers[0]->error->code,
                           "run_merge_task: error reading: %s",
                           readers[0]->error->message);
              return FALSE;
            }
          if (readers[0]->eof)
            goto restart_testing_eof;
#if DO_FLUSH
          if (is_finished)
            return TRUE;
#else
          if (n_written > iterations)
            return TRUE;
#endif
	}
#if HAS_MERGE
      else if (compare_rv == 0)
	{
	  /* merge and write merged key/value */
          guint value_len;
          const guint8 *value_data;
          switch (merge (MAYBE_LEN (readers[0]->key_len)
                         readers[0]->key_data,
                         MAYBE_LEN (readers[1]->value_len)
                         readers[1]->value_data,
                         MAYBE_LEN (readers[1]->value_len)
                         readers[1]->value_data,
                         merge_buf,
                         user_data))
            {
            case GSK_TABLE_MERGE_RETURN_A:
              value_len = readers[0]->value_len;
              value_data = readers[0]->value_data;
              break;
            case GSK_TABLE_MERGE_RETURN_B:
              value_len = readers[1]->value_len;
              value_data = readers[1]->value_data;
              break;
            case GSK_TABLE_MERGE_SUCCESS:
              value_len = merge_buf->len;
              value_data = merge_buf->data;
              break;
            case GSK_TABLE_MERGE_DROP:
              goto do_advance_a_and_b;
            default:
              g_assert_not_reached ();
            }

#if DO_SIMPLIFY
          switch (simplify (MAYBE_LEN (readers[0]->key_len)
                            readers[0]->key_data,
	                    MAYBE_LEN (value_len)
                            value_data,
                            simp_buffer,
                            user_data))
            {
            case GSK_TABLE_SIMPLIFY_IDENTITY:
              break;
            case GSK_TABLE_SIMPLIFY_SUCCESS:
              value_len = simp_buffer->len;
              value_data = simp_buffer->data;
              break;
            case GSK_TABLE_SIMPLIFY_DELETE:
              goto do_advance_a_and_b;
            default:
              g_assert_not_reached ();
            }
#endif
          n_written++;
          switch (gsk_table_file_feed_entry (output,
                                             readers[0]->key_len,
                                             readers[0]->key_data,
                                             value_len,
                                             value_data,
                                             error))
            {
            case GSK_TABLE_FEED_ENTRY_WANT_MORE:
              break;
            case GSK_TABLE_FEED_ENTRY_SUCCESS:
              set_buffer (&task->info.started.last_queryable_key,
                          readers[0]->key_len, readers[0]->key_data);
              task->info.started.has_last_queryable_key = TRUE;
#if DO_FLUSH
              if (n_written > iterations)
                is_finished = TRUE;
#endif
              break;
            case GSK_TABLE_FEED_ENTRY_ERROR:
              return FALSE;
            default:
              g_assert_not_reached ();
            }

do_advance_a_and_b:
          /* advance a and b, possibly becoming eof or error */
          gsk_table_reader_advance (readers[0]);
          gsk_table_reader_advance (readers[1]);
          if (readers[0]->error)
            {
              g_set_error (error, readers[0]->error->domain,
                           readers[0]->error->code,
                           "run_merge_task: error reading: %s",
                           readers[0]->error->message);
              return FALSE;
            }
          if (readers[1]->error)
            {
              g_set_error (error, readers[1]->error->domain,
                           readers[1]->error->code,
                           "run_merge_task: error reading: %s",
                           readers[1]->error->message);
              return FALSE;
            }
          if (readers[0]->eof || readers[1]->eof)
            goto restart_testing_eof;
#if DO_FLUSH
          if (is_finished)
            return TRUE;
#else
          if (n_written > iterations)
            return TRUE;
#endif
	}
#endif
      else
	{
	  /* write b */
          guint value_len;
          const guint8 *value_data;
#if DO_SIMPLIFY
	  switch (simplify (MAYBE_LEN (readers[1]->key_len)
                            readers[1]->key_data,
	                    MAYBE_LEN (readers[1]->value_len)
                            readers[1]->value_data,
                            simp_buffer,
                            user_data))
            {
            case GSK_TABLE_SIMPLIFY_IDENTITY:
              value_len = readers[1]->value_len;
              value_data = readers[1]->value_data;
              break;
            case GSK_TABLE_SIMPLIFY_SUCCESS:
              value_len = simp_buffer->len;
              value_data = simp_buffer->data;
              break;
            case GSK_TABLE_SIMPLIFY_DELETE:
              goto do_advance_b;
            default:
              g_assert_not_reached ();
            }
#else
          value_len = readers[1]->value_len;
          value_data = readers[1]->value_data;
#endif
          n_written++;
          switch (gsk_table_file_feed_entry (output,
                                             readers[1]->key_len,
                                             readers[1]->key_data,
                                             value_len,
                                             value_data,
                                             error))
            {
            case GSK_TABLE_FEED_ENTRY_WANT_MORE:
              break;
            case GSK_TABLE_FEED_ENTRY_SUCCESS:
              set_buffer (&task->info.started.last_queryable_key,
                          readers[1]->key_len, readers[1]->key_data);
              task->info.started.has_last_queryable_key = TRUE;
#if DO_FLUSH
              if (n_written > iterations)
                is_finished = TRUE;
#endif
              break;
            case GSK_TABLE_FEED_ENTRY_ERROR:
              return FALSE;
            default:
              g_assert_not_reached ();
            }

#if DO_SIMPLIFY
do_advance_b:
#endif
          /* advance b, possibly becoming eof or error */
          gsk_table_reader_advance (readers[1]);
          if (readers[1]->error)
            {
              g_set_error (error, readers[1]->error->domain,
                           readers[1]->error->code,
                           "run_merge_task: error reading: %s",
                           readers[1]->error->message);
              return FALSE;
            }
          if (readers[1]->eof)
            goto restart_testing_eof;
#if DO_FLUSH
          if (is_finished)
            return TRUE;
#else
          if (n_written > iterations)
            return TRUE;
#endif
	}
      goto restart_with_both_readers;
    }
  return TRUE;

done:
  return merge_task_done (table, task, error);
}

#undef MAYBE_LEN
#undef DO_SIMPLIFY
#undef DO_FLUSH
#undef HAS_LEN
#undef USE_MEMCMP
#undef HAS_MERGE
#undef MANGLE_FUNC_NAME
