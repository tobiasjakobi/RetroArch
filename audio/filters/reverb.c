/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2014 - Brad Miller
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
#include <string.h>

struct comb
{
   float *buffer;
   unsigned bufsize;
   unsigned bufidx;

   float feedback;
   float filterstore;
   float damp1, damp2;
};

static inline float comb_process(struct comb *c, float input)
{
   float output = c->buffer[c->bufidx];
   c->filterstore = (output * c->damp2) + (c->filterstore * c->damp1);

   c->buffer[c->bufidx] = input + (c->filterstore * c->feedback);

   c->bufidx++;
   if (c->bufidx >= c->bufsize)
      c->bufidx = 0;

   return output;
}

struct allpass
{
   float *buffer;
   float feedback;
   unsigned bufsize;
   unsigned bufidx;
};

static inline float allpass_process(struct allpass *a, float input)
{
   float bufout = a->buffer[a->bufidx];
   float output = -input + bufout;
   a->buffer[a->bufidx] = input + bufout * a->feedback;

   a->bufidx++;
   if (a->bufidx >= a->bufsize)
      a->bufidx = 0;

   return output;
}

#define numcombs 8
#define numallpasses 4
static const float muted = 0;
static const float fixedgain = 0.015f;
static const float scalewet = 3;
static const float scaledry = 2;
static const float scaledamp = 0.4f;
static const float scaleroom = 0.28f;
static const float offsetroom = 0.7f;
static const float initialroom = 0.5f;
static const float initialdamp = 0.5f;
static const float initialwet = 1.0f / 3.0f;
static const float initialdry = 0;
static const float initialwidth = 1;
static const float initialmode = 0;
static const float freezemode = 0.5f;

#define combtuningL1 1116
#define combtuningL2 1188
#define combtuningL3 1277
#define combtuningL4 1356
#define combtuningL5 1422
#define combtuningL6 1491
#define combtuningL7 1557
#define combtuningL8 1617
#define allpasstuningL1 556
#define allpasstuningL2 441
#define allpasstuningL3 341
#define allpasstuningL4 225

struct revmodel
{
   struct comb combL[numcombs];
   struct allpass allpassL[numallpasses];

   float bufcombL1[combtuningL1];
   float bufcombL2[combtuningL2];
   float bufcombL3[combtuningL3];
   float bufcombL4[combtuningL4];
   float bufcombL5[combtuningL5];
   float bufcombL6[combtuningL6];
   float bufcombL7[combtuningL7];
   float bufcombL8[combtuningL8];

   float bufallpassL1[allpasstuningL1];
   float bufallpassL2[allpasstuningL2];
   float bufallpassL3[allpasstuningL3];
   float bufallpassL4[allpasstuningL4];

   float gain;
   float roomsize, roomsize1;
   float damp, damp1;
   float wet, wet1, wet2;
   float dry;
   float width;
   float mode;
};

static float revmodel_process(struct revmodel *rev, float in)
{
   float mono_out = 0.0f;
   float mono_in = in;
   float input = mono_in * rev->gain;
   for (unsigned i = 0; i < numcombs; i++)
      mono_out += comb_process(&rev->combL[i], input);

   for (unsigned i = 0; i < numallpasses; i++)
      mono_out = allpass_process(&rev->allpassL[i], mono_out);

   return mono_in * rev->dry + mono_out * rev->wet1;
}

static void revmodel_update(struct revmodel *rev)
{
   rev->wet1 = rev->wet * (rev->width / 2.0f + 0.5f);

   if (rev->mode >= freezemode)
   {
      rev->roomsize1 = 1.0f;
      rev->damp1 = 0.0f;
      rev->gain = muted;
   }
   else
   {
      rev->roomsize1 = rev->roomsize;
      rev->damp1 = rev->damp;
      rev->gain = fixedgain;
   }

   for (unsigned i = 0; i < numcombs; i++)
   {
      rev->combL[i].feedback = rev->roomsize1;
      rev->combL[i].damp1 = rev->damp1;
      rev->combL[i].damp2 = 1.0f - rev->damp1;
   }
}

static void revmodel_setroomsize(struct revmodel *rev, float value)
{
   rev->roomsize = value * scaleroom + offsetroom;
   revmodel_update(rev);
}

static void revmodel_setdamp(struct revmodel *rev, float value)
{
   rev->damp = value * scaledamp;
   revmodel_update(rev);
}

static void revmodel_setwet(struct revmodel *rev, float value)
{
   rev->wet = value * scalewet;
   revmodel_update(rev);
}

static void revmodel_setdry(struct revmodel *rev, float value)
{
   rev->dry = value * scaledry;
   revmodel_update(rev);
}

static void revmodel_setwidth(struct revmodel *rev, float value)
{
   rev->width = value;
   revmodel_update(rev);
}

static void revmodel_setmode(struct revmodel *rev, float value)
{
   rev->mode = value;
   revmodel_update(rev);
}

static void revmodel_init(struct revmodel *rev)
{
   rev->combL[0].buffer = rev->bufcombL1; rev->combL[0].bufsize = combtuningL1;
   rev->combL[1].buffer = rev->bufcombL2; rev->combL[1].bufsize = combtuningL2;
   rev->combL[2].buffer = rev->bufcombL3; rev->combL[2].bufsize = combtuningL3;
   rev->combL[3].buffer = rev->bufcombL4; rev->combL[3].bufsize = combtuningL4;
   rev->combL[4].buffer = rev->bufcombL5; rev->combL[4].bufsize = combtuningL5;
   rev->combL[5].buffer = rev->bufcombL6; rev->combL[5].bufsize = combtuningL6;
   rev->combL[6].buffer = rev->bufcombL7; rev->combL[6].bufsize = combtuningL7;
   rev->combL[7].buffer = rev->bufcombL8; rev->combL[7].bufsize = combtuningL8;

   rev->allpassL[0].buffer = rev->bufallpassL1; rev->allpassL[0].bufsize = allpasstuningL1;
   rev->allpassL[1].buffer = rev->bufallpassL2; rev->allpassL[1].bufsize = allpasstuningL2;
   rev->allpassL[2].buffer = rev->bufallpassL3; rev->allpassL[2].bufsize = allpasstuningL3;
   rev->allpassL[3].buffer = rev->bufallpassL4; rev->allpassL[3].bufsize = allpasstuningL4;

   rev->allpassL[0].feedback = 0.5f;
   rev->allpassL[1].feedback = 0.5f;
   rev->allpassL[2].feedback = 0.5f;
   rev->allpassL[3].feedback = 0.5f;

   revmodel_setwet(rev, initialwet);
   revmodel_setroomsize(rev, initialroom);
   revmodel_setdry(rev, initialdry);
   revmodel_setdamp(rev, initialdamp);
   revmodel_setwidth(rev, initialwidth);
   revmodel_setmode(rev, initialmode);
}

struct reverb_data
{
   struct revmodel left, right;
};

static void reverb_free(void *data)
{
   free(data);
}

static void reverb_process(void *data, struct dspfilter_output *output,
      const struct dspfilter_input *input)
{
   struct reverb_data *rev = data;

   output->samples = input->samples;
   output->frames  = input->frames;
   float *out = output->samples;

   for (unsigned i = 0; i < input->frames; i++, out += 2)
   {
      float in[2] = { out[0], out[1] };

      out[0] = revmodel_process(&rev->left, in[0]);
      out[1] = revmodel_process(&rev->right, in[1]);
   }
}

static void *reverb_init(const struct dspfilter_info *info,
      const struct dspfilter_config *config, void *userdata)
{
   struct reverb_data *rev = calloc(1, sizeof(*rev));
   if (!rev)
      return NULL;

   float drytime, wettime, damping, roomwidth, roomsize;
   config->get_float(userdata, "drytime", &drytime, 0.43f);
   config->get_float(userdata, "wettime", &wettime, 0.4f);
   config->get_float(userdata, "damping", &damping, 0.8f);
   config->get_float(userdata, "roomwidth", &roomwidth, 0.56f);
   config->get_float(userdata, "roomsize", &roomsize, 0.56f);

   revmodel_init(&rev->left);
   revmodel_init(&rev->right);

   revmodel_setdamp(&rev->left, damping);
   revmodel_setdry(&rev->left, drytime);
   revmodel_setwet(&rev->left, wettime);
   revmodel_setwidth(&rev->left, roomwidth);
   revmodel_setroomsize(&rev->left, roomsize);

   revmodel_setdamp(&rev->right, damping);
   revmodel_setdry(&rev->right, drytime);
   revmodel_setwet(&rev->right, wettime);
   revmodel_setwidth(&rev->right, roomwidth);
   revmodel_setroomsize(&rev->right, roomsize);

   return rev;
}

static const struct dspfilter_implementation reverb_plug = {
   reverb_init,
   reverb_process,
   reverb_free,

   DSPFILTER_API_VERSION,
   "Reverb",
   "reverb",
};

const struct dspfilter_implementation *dspfilter_get_implementation(dspfilter_simd_mask_t mask)
{
   (void)mask;
   return &reverb_plug;
}

#undef dspfilter_get_implementation


