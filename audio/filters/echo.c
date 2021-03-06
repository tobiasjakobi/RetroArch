/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
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

#include "dspfilter.h"
#include <math.h>
#include <stdlib.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

struct echo_channel
{
   float *buffer;
   unsigned ptr;
   unsigned frames;
   float feedback;
};

struct echo_data
{
   struct echo_channel *channels;
   unsigned num_channels;
   float amp;
};

static void echo_free(void *data)
{
   struct echo_data *echo = data;

   for (unsigned i = 0; i < echo->num_channels; i++)
      free(echo->channels[i].buffer);
   free(echo->channels);
   free(echo);
}

static void echo_process(void *data, struct dspfilter_output *output,
      const struct dspfilter_input *input)
{
   struct echo_data *echo = data;

   output->samples = input->samples;
   output->frames  = input->frames;

   float *out = output->samples;

   for (unsigned i = 0; i < input->frames; i++, out += 2)
   {
      float echo_left  = 0.0f;
      float echo_right = 0.0f;

      for (unsigned c = 0; c < echo->num_channels; c++)
      {
         echo_left  += echo->channels[c].buffer[(echo->channels[c].ptr << 1) + 0];
         echo_right += echo->channels[c].buffer[(echo->channels[c].ptr << 1) + 1];
      }

      echo_left  *= echo->amp;
      echo_right *= echo->amp;

      float left  = out[0] + echo_left;
      float right = out[1] + echo_right;

      for (unsigned c = 0; c < echo->num_channels; c++)
      {
         float feedback_left  = out[0] + echo->channels[c].feedback * echo_left;
         float feedback_right = out[1] + echo->channels[c].feedback * echo_right;

         echo->channels[c].buffer[(echo->channels[c].ptr << 1) + 0] = feedback_left;
         echo->channels[c].buffer[(echo->channels[c].ptr << 1) + 1] = feedback_right;

         echo->channels[c].ptr = (echo->channels[c].ptr + 1) % echo->channels[c].frames;
      }

      out[0] = left;
      out[1] = right;
   }
}

static void *echo_init(const struct dspfilter_info *info,
      const struct dspfilter_config *config, void *userdata)
{
   struct echo_data *echo = calloc(1, sizeof(*echo));
   if (!echo)
      return NULL;

   float *delay = NULL, *feedback = NULL;
   unsigned num_delay = 0, num_feedback = 0;

   static const float default_delay[] = { 200.0f };
   static const float default_feedback[] = { 0.5f };

   config->get_float_array(userdata, "delay", &delay, &num_delay, default_delay, 1);
   config->get_float_array(userdata, "feedback", &feedback, &num_feedback, default_feedback, 1);
   config->get_float(userdata, "amp", &echo->amp, 0.2f);

   unsigned channels = num_feedback = num_delay = min(num_delay, num_feedback);

   echo->channels = calloc(channels, sizeof(*echo->channels));
   if (!echo->channels)
      goto error;

   echo->num_channels = channels;

   for (unsigned i = 0; i < channels; i++)
   {
      unsigned frames = (unsigned)(delay[i] * info->input_rate / 1000.0f + 0.5f);
      if (!frames)
         goto error;

      echo->channels[i].buffer = calloc(frames, 2 * sizeof(float));
      if (!echo->channels[i].buffer)
         goto error;

      echo->channels[i].frames = frames;
      echo->channels[i].feedback = feedback[i];
   }

   config->free(delay);
   config->free(feedback);
   return echo;

error:
   config->free(delay);
   config->free(feedback);
   echo_free(echo);
   return NULL;
}


static const struct dspfilter_implementation echo_plug = {
   echo_init,
   echo_process,
   echo_free,

   DSPFILTER_API_VERSION,
   "Multi-Echo",
   "echo",
};

const struct dspfilter_implementation *dspfilter_get_implementation(dspfilter_simd_mask_t mask)
{
   (void)mask;
   return &echo_plug;
}

#undef dspfilter_get_implementation

