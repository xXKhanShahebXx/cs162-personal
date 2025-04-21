/**
 * Logic for job and task management.
 *
 * You are not required to modify this file.
 */

#ifndef JOB_H__
#define JOB_H__

/* You may add definitions here */
#include "../lib/lib.h"
#include "../app/app.h"
#include <glib.h>
#include <time.h>    

typedef enum {
    JOB_PENDING,                 
    JOB_RUNNING,                 
    JOB_DONE,                   
    JOB_FAILED            
  } job_status;
  

  typedef enum {
    TASK_IDLE,
    TASK_IN_PROGRESS,
    TASK_FINISHED
  } task_state;
  
 
  typedef struct job_t {
    int           id;          
    app           application;  
    GPtrArray*    files;         
    int           n_map;          
    int           n_reduce;
    char*         output_dir;    
    sized_buffer  args;           
    char*         app_name;
    task_state*   map_tasks;
    time_t*       map_start_times;      
    task_state*   reduce_tasks;
    time_t*       reduce_start_times;
    job_status    status;
  } job;
#endif
