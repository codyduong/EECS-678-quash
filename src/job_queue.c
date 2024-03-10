#include <job_queue.h>

void free_job(Job j) {
  free(j.cmd_str);
  free(j.pids);
}
