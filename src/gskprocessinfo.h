#ifndef __GSK_PROCESS_INFO_H_
#define __GSK_PROCESS_INFO_H_

#include <glib.h>

typedef enum
{
  GSK_PROCESS_INFO_RUNNING = 'R',
  GSK_PROCESS_INFO_SLEEPING = 'S',
  GSK_PROCESS_INFO_DISK = 'D',
  GSK_PROCESS_INFO_ZOMBIE = 'Z',
  GSK_PROCESS_INFO_TRACED = 'T',
  GSK_PROCESS_INFO_PAGING = 'W'
} GskProcessInfoState;
const char *gsk_process_info_state_name (GskProcessInfoState);

typedef struct _GskProcessInfo GskProcessInfo;
struct _GskProcessInfo
{
  guint process_id;
  guint parent_process_id;
  guint process_group_id;
  guint session_id;
  guint tty_id;
  gulong user_runtime;
  gulong kernel_runtime;
  gulong child_user_runtime;		/* only includes reaped children */
  gulong child_kernel_runtime;		/* only includes reaped children */
  guint nice;
  GskProcessInfoState state;
  gulong virtual_memory_size;
  gulong resident_memory_size;
  char *exe_filename;
};

GskProcessInfo *gsk_process_info_get  (guint           pid,
                                       GError        **error);
void             gsk_process_info_free(GskProcessInfo *info);

#endif
