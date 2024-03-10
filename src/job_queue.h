#include <deque.h>
#include "command.h"

typedef struct {
  int job_id;
  pid_t* pids;
  int num_pids;
  // Command cmd;
  char* cmd_str;
} Job;

void free_job(Job j);

IMPLEMENT_DEQUE_STRUCT(JobQueue, Job);
PROTOTYPE_DEQUE(JobQueue, Job)
