/*  RetroArch - A frontend for libretro.
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

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>

#include<libtransistor/nx.h>
#include<libtransistor/alloc_pages.h>

#include "../audio_driver.h"
#include "../../verbosity.h"

static const int sample_rate           = 48000;
static const int max_num_samples       = sample_rate;
static const int num_channels          = 2;
static const size_t sample_buffer_size = ((max_num_samples * num_channels * sizeof(uint16_t)) + 0xfff) & ~0xfff;

typedef struct
{
   bool blocking;
   bool is_paused;
   uint64_t last_append;
   unsigned latency;
   audio_output_buffer_t buffers[3];
   audio_output_buffer_t *current_buffer;

   audio_output_t output;
   handle_t event;
} switch_audio_t;

static ssize_t switch_audio_write(void *data, const void *buf, size_t size)
{
   size_t to_write     = size;
	switch_audio_t *swa = (switch_audio_t*) data;

   if (!swa)
      return -1;

	if (!swa->current_buffer)
   {
      uint32_t num;
      if (audio_ipc_output_get_released_buffer(
               &swa->output, &num, &swa->current_buffer) != 0)
      {
         RARCH_LOG("Failed to get released buffer?\n");
         return -1;
      }

      if (num < 1)
         swa->current_buffer = NULL;

      if (!swa->current_buffer)
      {
         if (swa->blocking)
         {
            RARCH_LOG("No buffer, blocking...\n");

            while(swa->current_buffer == NULL)
            {
               uint32_t handle_idx = 0;
               num                 = 0;

               svcWaitSynchronization(&handle_idx, &swa->event, 1, 33333333);
               svcResetSignal(swa->event);

               if (audio_ipc_output_get_released_buffer(&swa->output, &num, &swa->current_buffer) != 0)
                  return -1;
            }
         }
         else
            /* no buffer, nonblocking... */
            return 0;
      }

      swa->current_buffer->data_size = 0;
   }

	if (to_write > sample_buffer_size - swa->current_buffer->data_size)
		to_write = sample_buffer_size - swa->current_buffer->data_size;
	
	memcpy(((uint8_t*) swa->current_buffer->sample_data) + swa->current_buffer->data_size, buf, to_write);
	swa->current_buffer->data_size   += to_write;
	swa->current_buffer->buffer_size  = sample_buffer_size;

	if (swa->current_buffer->data_size > (48000*swa->latency)/1000)
   {
		if (audio_ipc_output_append_buffer(&swa->output, swa->current_buffer) 
            != 0)
			return -1;
		swa->current_buffer = NULL;
	}

	swa->last_append = svcGetSystemTick();
	
	return to_write;
}

static bool switch_audio_stop(void *data)
{
   switch_audio_t *swa = (switch_audio_t*) data;
   if (!swa)
      return false;

   if(!swa->is_paused)
	   if(audio_ipc_output_stop(&swa->output) != 0)
		   return false;

   swa->is_paused = true;
   return true;
}

static bool switch_audio_start(void *data, bool is_shutdown)
{
   switch_audio_t *swa = (switch_audio_t*) data;

   if(swa->is_paused)
	   if (audio_ipc_output_start(&swa->output) != 0)
		   return false;

   swa->is_paused = false;
   return true;
}

static bool switch_audio_alive(void *data)
{
   switch_audio_t *swa = (switch_audio_t*) data;
   if (!swa)
      return false;
   return !swa->is_paused;
}

static void switch_audio_free(void *data)
{
   switch_audio_t *swa = (switch_audio_t*) data;

   if (!swa)
      return;

   audio_ipc_output_close(&swa->output);
   audio_ipc_finalize();
   free(swa);
}

static bool switch_audio_use_float(void *data)
{
	(void) data;
	return false; /* force INT16 */
}

static size_t switch_audio_write_avail(void *data)
{
   switch_audio_t *swa = (switch_audio_t*) data;

   if (!swa || !swa->current_buffer)
      return 0;

   return swa->current_buffer->buffer_size;
}

static void switch_audio_set_nonblock_state(void *data, bool state)
{
   switch_audio_t *swa = (switch_audio_t*) data;

   if (swa)
      swa->blocking = !state;
}

static void *switch_audio_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   unsigned i;
   char names[8][0x20];
   uint32_t num_names  = 0;
   switch_audio_t *swa = (switch_audio_t*) calloc(1, sizeof(*swa));

   if (!swa)
      return NULL;

   if (audio_ipc_init() != 0)
      goto fail;

   if (audio_ipc_list_outputs(&names[0], 8, &num_names) != 0)
      goto fail_audio_ipc;

   if (num_names != 1)
   {
      RARCH_ERR("got back more than one AudioOut\n");
      goto fail_audio_ipc;
   }

   if (audio_ipc_open_output(names[0], &swa->output) != 0)
      goto fail_audio_ipc;

   if (swa->output.sample_rate != sample_rate)
   {
      RARCH_ERR("expected sample rate of %d, got sample rate of %d\n",
            sample_rate, swa->output.sample_rate);
      goto fail_audio_output;
   }

   if (swa->output.num_channels != num_channels)
   {
      RARCH_ERR("expected %d channels, got %d\n", num_channels,
            swa->output.num_channels);
      goto fail_audio_output;
   }

   if (swa->output.sample_format != PCM_INT16)
   {
      RARCH_ERR("expected PCM_INT16, got %d\n", swa->output.sample_format);
      goto fail_audio_output;
   }

   if (audio_ipc_output_register_buffer_event(&swa->output, &swa->event) != 0)
      goto fail_audio_output;

   for(i = 0; i < 3; i++)
   {
      swa->buffers[i].ptr         = &swa->buffers[i].sample_data;
      swa->buffers[i].sample_data = alloc_pages(sample_buffer_size, sample_buffer_size, NULL);
      swa->buffers[i].buffer_size = sample_buffer_size;
      swa->buffers[i].data_size   = sample_buffer_size;
      swa->buffers[i].unknown     = 0;

      if(swa->buffers[i].sample_data == NULL)
	      goto fail_audio_output;
      
      if (audio_ipc_output_append_buffer(&swa->output, &swa->buffers[i]) != 0)
         goto fail_audio_output;
   }

   *new_rate           = swa->output.sample_rate;

   swa->current_buffer = NULL;
   swa->latency        = latency;
   swa->last_append    = svcGetSystemTick();

   swa->blocking       = block_frames;
   swa->is_paused      = true;

   RARCH_LOG("[Audio]: Audio initialized\n");
   
   return swa;

fail_audio_output:
   audio_ipc_output_close(&swa->output);
fail_audio_ipc:
   audio_ipc_finalize();
fail:
   if (swa)
      free(swa);
   return NULL;
}

static size_t switch_audio_buffer_size(void *data)
{
   (void) data;
   return sample_buffer_size;
}

audio_driver_t audio_switch = {
   switch_audio_init,
   switch_audio_write,
   switch_audio_stop,
   switch_audio_start,
   switch_audio_alive,
   switch_audio_set_nonblock_state,
   switch_audio_free,
   switch_audio_use_float,
   "switch",
   NULL, /* device_list_new */
   NULL, /* device_list_free */
   switch_audio_write_avail,
   switch_audio_buffer_size, /* buffer_size */
};
