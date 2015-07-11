/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "thread.h"
#include <stdlib.h>

#include <pthread.h>
#include <time.h>

struct thread_data
{
   void (*func)(void*);
   void *userdata;
};

struct sthread
{
   pthread_t id;
};

static void *thread_wrap(void *data_)
{
   struct thread_data *data = (struct thread_data*)data_;
   data->func(data->userdata);
   free(data);
   return NULL;
}

sthread_t *sthread_create(void (*thread_func)(void*), void *userdata)
{
   sthread_t *thr = (sthread_t*)calloc(1, sizeof(*thr));
   if (!thr)
      return NULL;

   struct thread_data *data = (struct thread_data*)calloc(1, sizeof(*data));
   if (!data)
   {
      free(thr);
      return NULL;
   }

   data->func = thread_func;
   data->userdata = userdata;

   if (pthread_create(&thr->id, NULL, thread_wrap, data) < 0)
   {
      free(data);
      free(thr);
      return NULL;
   }

   return thr;
}

int sthread_detach(sthread_t *thread)
{
   return pthread_detach(thread->id);
}

void sthread_join(sthread_t *thread)
{
   pthread_join(thread->id, NULL);
   free(thread);
}

struct slock
{
   pthread_mutex_t lock;
};

slock_t *slock_new(void)
{
   slock_t *lock = (slock_t*)calloc(1, sizeof(*lock));
   if (!lock)
      return NULL;

   if (pthread_mutex_init(&lock->lock, NULL) < 0)
   {
      free(lock);
      return NULL;
   }

   return lock;
}

void slock_free(slock_t *lock)
{
   pthread_mutex_destroy(&lock->lock);
   free(lock);
}

void slock_lock(slock_t *lock)
{
   pthread_mutex_lock(&lock->lock);
}

void slock_unlock(slock_t *lock)
{
   pthread_mutex_unlock(&lock->lock);
}

struct scond
{
   pthread_cond_t cond;
};

scond_t *scond_new(void)
{
   scond_t *cond = (scond_t*)calloc(1, sizeof(*cond));
   if (!cond)
      return NULL;

   if (pthread_cond_init(&cond->cond, NULL) < 0)
   {
      free(cond);
      return NULL;
   }

   return cond;
}

void scond_free(scond_t *cond)
{
   pthread_cond_destroy(&cond->cond);
   free(cond);
}

void scond_wait(scond_t *cond, slock_t *lock)
{
   pthread_cond_wait(&cond->cond, &lock->lock);
}

int scond_broadcast(scond_t *cond)
{
   return pthread_cond_broadcast(&cond->cond);
}

bool scond_wait_timeout(scond_t *cond, slock_t *lock, int64_t timeout_us)
{
   struct timespec now = {0};
   int ret;
   int64_t seconds, remainder;

   clock_gettime(CLOCK_REALTIME, &now);

   seconds = timeout_us / 1000000LL;
   remainder = timeout_us % 1000000LL;

   now.tv_sec += seconds;
   now.tv_nsec += remainder * 1000LL;

   ret = pthread_cond_timedwait(&cond->cond, &lock->lock, &now);
   return (ret == 0);
}

void scond_signal(scond_t *cond)
{
   pthread_cond_signal(&cond->cond);
}

