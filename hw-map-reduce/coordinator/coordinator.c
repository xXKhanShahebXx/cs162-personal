/**
 * The MapReduce coordinator.
 */

#include "coordinator.h"

#ifndef SIG_PF
#define SIG_PF void (*)(int)
#endif

/* Global coordinator state. */
coordinator* state;

extern void coordinator_1(struct svc_req*, SVCXPRT*);

static inline char* dup_str(const char* s) { return s ? g_strdup(s) : NULL; }

static void copy_args(get_task_reply* r, const sized_buffer* src) {
  r->args.args_len = src->length;
  if (src->length == 0) {
    r->args.args_val = NULL;
  } else {
    r->args.args_val = malloc(src->length);
    memcpy(r->args.args_val, src->buffer, src->length);
  }
}

static bool job_maps_done(const job* j) {
  for (int i = 0; i < j->n_map; ++i)
    if (j->map_tasks[i] != TASK_FINISHED) return false;
  return true;
}

static bool job_reduces_done(const job* j) {
  for (int i = 0; i < j->n_reduce; ++i)
    if (j->reduce_tasks[i] != TASK_FINISHED) return false;
  return true;
}

static void maybe_finish_job(job* j) {
  if (j->status == JOB_FAILED) return;
  if (job_maps_done(j) && job_reduces_done(j)) {
    j->status = JOB_DONE;
    printf("Job‑%d finished successfully\n", j->id);
  }
}

/* Set up and run RPC server. */
int main(int argc, char** argv) {
  register SVCXPRT* transp;

  pmap_unset(COORDINATOR, COORDINATOR_V1);

  transp = svcudp_create(RPC_ANYSOCK);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create udp service.");
    exit(1);
  }
  if (!svc_register(transp, COORDINATOR, COORDINATOR_V1, coordinator_1, IPPROTO_UDP)) {
    fprintf(stderr, "%s", "unable to register (COORDINATOR, COORDINATOR_V1, udp).");
    exit(1);
  }

  transp = svctcp_create(RPC_ANYSOCK, 0, 0);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create tcp service.");
    exit(1);
  }
  if (!svc_register(transp, COORDINATOR, COORDINATOR_V1, coordinator_1, IPPROTO_TCP)) {
    fprintf(stderr, "%s", "unable to register (COORDINATOR, COORDINATOR_V1, tcp).");
    exit(1);
  }

  coordinator_init(&state);

  svc_run();
  fprintf(stderr, "%s", "svc_run returned");
  exit(1);
  /* NOTREACHED */
}

/* EXAMPLE RPC implementation. */
int* example_1_svc(int* argp, struct svc_req* rqstp) {
  static int result;

  result = *argp + 1;

  return &result;
}

/* SUBMIT_JOB RPC implementation. */
int* submit_job_1_svc(submit_job_request* argp, struct svc_req* rqstp) {
  static int result;

  printf("Received submit job request\n");

  /* TODO */
  app application = get_app(argp->app);
  if (application.name == NULL) {              
    result = -1;
    return &result;
  }

  job* j = malloc(sizeof(job));
  j->id           = state->next_job_id++;
  j->application  = application;
  j->app_name     = strdup(argp->app);
  j->output_dir   = strdup(argp->output_dir);

  j->files = g_ptr_array_new_with_free_func(free);
  for (u_int i = 0; i < argp->files.files_len; ++i)
    g_ptr_array_add(j->files, strdup(argp->files.files_val[i]));

  j->n_map        = j->files->len;
  j->n_reduce     = argp->n_reduce;

  j->map_tasks    = calloc(j->n_map,    sizeof(task_state));
  j->reduce_tasks = calloc(j->n_reduce, sizeof(task_state));

  j->map_start_times    = calloc(j->n_map, sizeof(time_t));   
  j->reduce_start_times = calloc(j->n_reduce, sizeof(time_t));

  j->args.length  = argp->args.args_len;
  if (j->args.length > 0) {
    j->args.buffer = malloc(j->args.length);
    memcpy(j->args.buffer, argp->args.args_val, j->args.length);
  } else {
    j->args.buffer = NULL;
  }

  j->status = JOB_PENDING;

  g_hash_table_insert(state->jobs, GINT_TO_POINTER(j->id), j);
  g_queue_push_tail(state->job_queue, j);

  result = j->id;                              
  /* Do not modify the following code. */
  /* BEGIN */
  struct stat st;
  if (stat(argp->output_dir, &st) == -1) {
    mkdirp(argp->output_dir);
  }

  return &result;
  /* END */
}

/* POLL_JOB RPC implementation. */
poll_job_reply* poll_job_1_svc(int* argp, struct svc_req* rqstp) {
  static poll_job_reply result;

  printf("Received poll job request\n");

  /* TODO */
  result.done = result.failed = result.invalid_job_id = false;
  job* j = g_hash_table_lookup(state->jobs, GINT_TO_POINTER(*argp));
  if (!j) { result.invalid_job_id = true; return &result; }

  if (j->status == JOB_DONE)  result.done = true;
  if (j->status == JOB_FAILED){ result.done = true; result.failed = true; }

  return &result;
}

/* GET_TASK RPC implementation. */
get_task_reply* get_task_1_svc(void* argp, struct svc_req* rqstp) {
  static get_task_reply result;

  printf("Received get task request\n");

  result.wait = true;
  result.job_id = result.task = -1;
  result.file = dup_str("");
  result.output_dir = dup_str("");
  result.app = dup_str("");
  result.n_map = result.n_reduce = 0;
  result.reduce = false;
  result.args.args_len = 0;
  result.args.args_val = NULL;


  time_t now = time(NULL);

  /* TODO */
  for (GList* it = state->job_queue->head; it; it = it->next) {
    job* j = it->data;

    if (j->status == JOB_FAILED) continue;


    for (int i = 0; i < j->n_map; i++) {
      if (j->map_tasks[i] == TASK_IN_PROGRESS && now - j->map_start_times[i] > TASK_TIMEOUT_SECS) {
          j->map_tasks[i] = TASK_IDLE;
      }
    }

    for (int i = 0; i < j->n_map; ++i) {
      if (j->map_tasks[i] == TASK_IDLE) {
        result.wait        = false;
        result.reduce      = false;
        result.job_id      = j->id;
        result.task        = i;
        result.file        = dup_str(j->files->pdata[i]);
        result.output_dir  = dup_str(j->output_dir);
        result.app         = dup_str(j->app_name);
        result.n_map       = j->n_map;
        result.n_reduce    = j->n_reduce;
        copy_args(&result, &j->args);

        j->map_tasks[i] = TASK_IN_PROGRESS;
        j->map_start_times[i] = now;
        j->status       = JOB_RUNNING;
        return &result;
      }
    }


    if (!job_maps_done(j)) continue;

    for (int r = 0; r < j->n_reduce; r++) {
      if (j->reduce_tasks[r] == TASK_IN_PROGRESS && now - j->reduce_start_times[r] > TASK_TIMEOUT_SECS) {
          j->reduce_tasks[r] = TASK_IDLE;
      }
    }

    for (int r = 0; r < j->n_reduce; ++r) {
      if (j->reduce_tasks[r] == TASK_IDLE) {
        result.wait        = false;
        result.reduce      = true;
        result.job_id      = j->id;
        result.task        = r;
        result.file        = dup_str("");       
        result.output_dir  = dup_str(j->output_dir);
        result.app         = dup_str(j->app_name);
        result.n_map       = j->n_map;
        result.n_reduce    = j->n_reduce;
        copy_args(&result, &j->args);

        j->reduce_tasks[r] = TASK_IN_PROGRESS;
        j->reduce_start_times[r] = now;
        j->status          = JOB_RUNNING;
        return &result;
      }
    }

    maybe_finish_job(j);
  }

  return &result;
}

/* FINISH_TASK RPC implementation. */
void* finish_task_1_svc(finish_task_request* argp, struct svc_req* rqstp) {
  static char* result;

  printf("Received finish task request\n");

  /* TODO */
  job* j = g_hash_table_lookup(state->jobs, GINT_TO_POINTER(argp->job_id));
  if (!j) return (void*)&result;

  if (j->status == JOB_DONE || j->status == JOB_FAILED) return (void*)&result;

  int t = argp->task;
  if ((!argp->reduce && (t < 0 || t >= j->n_map)) || ( argp->reduce && (t < 0 || t >= j->n_reduce)))
    return (void*)&result;

  if (!argp->success) {
    j->status = JOB_FAILED;
    printf("Job‑%d marked FAILED (task %d, %s)\n", j->id, t, argp->reduce ? "reduce" : "map");
    return (void*)&result;
  }

  if (!argp->reduce) j->map_tasks[t] = TASK_FINISHED;
  else j->reduce_tasks[t] = TASK_FINISHED;

  maybe_finish_job(j);

  return (void*)&result;
}

/* Initialize coordinator state. */
void coordinator_init(coordinator** coord_ptr) {
  *coord_ptr = malloc(sizeof(coordinator));
  (*coord_ptr)->next_job_id = 0;
  (*coord_ptr)->jobs = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
  (*coord_ptr)->job_queue = g_queue_new();

  /* TODO */
}
