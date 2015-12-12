/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2013-2014 - Jason Fetters
 *  Copyright (C) 2014-2015 - Daniel De Matteis
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <boolean.h>

#include "../../driver.h"
#include "joypad_connection.h"

struct wiiupro_buttons
{
   bool a;
   bool b;
   bool x;
   bool y;
   bool l;
   bool r;
   bool zl;
   bool zr;
   bool minus;
   bool plus;
   bool l3;
   bool r3;
   bool home;

   /* D-Pad */
   bool left;
   bool right;
   bool up;
   bool down;
}__attribute__((packed));

struct wiiupro
{
   uint8_t hatvalue[4];
   struct wiiupro_buttons btn;
};

struct hidpad_wiiupro_data
{
   struct pad_connection* connection;
   send_control_t send_control;
   struct wiiupro data;
   uint32_t slot;
   bool have_led;
   uint16_t motors[2];
};

static void hidpad_wiiupro_send_control(struct hidpad_wiiupro_data* device)
{
   /* 0x15 - Request status */
   static uint8_t report_buffer[4] = { 0xA2, 0x15, 0x00 };
   device->send_control(device->connection, report_buffer, sizeof(report_buffer));
}

static void* hidpad_wiiupro_init(void *data, uint32_t slot, send_control_t ptr)
{
   struct pad_connection* connection = (struct pad_connection*)data;
   struct hidpad_wiiupro_data* device    = (struct hidpad_wiiupro_data*)
      calloc(1, sizeof(struct hidpad_wiiupro_data));

   if (!device)
      goto error;

   if (!connection)
      goto error;

   device->connection   = connection;
   device->slot         = slot;
   device->send_control = ptr;

   /* Without this, the digital buttons won't be reported. */
   hidpad_wiiupro_send_control(device);

   return device;

error:
   if (device)
      free(device);
   return NULL;
}

static void hidpad_wiiupro_deinit(void *data)
{
   struct hidpad_wiiupro_data *device = (struct hidpad_wiiupro_data*)data;

   if (device)
      free(device);
}

static uint64_t hidpad_wiiupro_get_buttons(void *data)
{
   uint64_t buttonstate           = 0;
   struct hidpad_wiiupro_data *device = (struct hidpad_wiiupro_data*)data;
   struct wiiupro *rpt = device ? (struct wiiupro*)&device->data : NULL;

   if (!device || !rpt)
      return 0;

   buttonstate |= (rpt->btn.r3       ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R3)     : 0);
   buttonstate |= (rpt->btn.l3       ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L3)     : 0);
   buttonstate |= (rpt->btn.plus     ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_START)  : 0);
   buttonstate |= (rpt->btn.minus    ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_SELECT) : 0);
   buttonstate |= (rpt->btn.zr       ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R2)     : 0);
   buttonstate |= (rpt->btn.zl       ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L2)     : 0);
   buttonstate |= (rpt->btn.r        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R)      : 0);
   buttonstate |= (rpt->btn.l        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L)      : 0);

   buttonstate |= (rpt->btn.x        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_X)      : 0);
   buttonstate |= (rpt->btn.a        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_A)      : 0);
   buttonstate |= (rpt->btn.b        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_B)      : 0);
   buttonstate |= (rpt->btn.y        ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_Y)      : 0);
   buttonstate |= (rpt->btn.left     ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_LEFT)   : 0);
   buttonstate |= (rpt->btn.right    ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_RIGHT)  : 0);
   buttonstate |= (rpt->btn.up       ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_UP)     : 0);
   buttonstate |= (rpt->btn.down     ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_DOWN)   : 0);
   buttonstate |= (rpt->btn.home     ? (UINT64_C(1) << RARCH_MENU_TOGGLE)             : 0);

   return buttonstate;
}

static int16_t hidpad_wiiupro_get_axis(void *data, unsigned axis)
{
   /* stub out for now */

   return 0;
}

static void hidpad_wiiupro_packet_handler(void *data, uint8_t *packet, uint16_t size)
{
   struct hidpad_wiiupro_data *device = (struct hidpad_wiiupro_data*)data;

   if (!device)
      return;

#if 0
   if (!device->have_led)
   {
      hidpad_wiiupro_send_control(device);
      device->have_led = true;
   }
#endif

   packet[0x0C] ^= 0xFF;
   packet[0x0D] ^= 0xFF;
   packet[0x0E] ^= 0xFF;

   memset(&device->data, 0, sizeof(struct wiiupro));

   device->data.btn.b     = (packet[0x0D] & 0x40) ? 1 : 0;
   device->data.btn.a     = (packet[0x0D] & 0x10) ? 1 : 0;
   device->data.btn.y     = (packet[0x0D] & 0x20) ? 1 : 0;
   device->data.btn.x     = (packet[0x0D] & 0x08) ? 1 : 0;
   device->data.btn.l     = (packet[0x0C] & 0x20) ? 1 : 0;
   device->data.btn.r     = (packet[0x0C] & 0x02) ? 1 : 0;
   device->data.btn.zl    = (packet[0x0D] & 0x80) ? 1 : 0;
   device->data.btn.zr    = (packet[0x0D] & 0x04) ? 1 : 0;
   device->data.btn.minus = (packet[0x0C] & 0x10) ? 1 : 0;
   device->data.btn.plus  = (packet[0x0C] & 0x04) ? 1 : 0;
   device->data.btn.l3    = (packet[0x0E] & 0x02) ? 1 : 0;
   device->data.btn.r3    = (packet[0x0E] & 0x01) ? 1 : 0;

   device->data.btn.left  = (packet[0x0D] & 0x02) ? 1 : 0;
   device->data.btn.right = (packet[0x0C] & 0x80) ? 1 : 0;
   device->data.btn.up    = (packet[0x0D] & 0x01) ? 1 : 0;
   device->data.btn.down  = (packet[0x0C] & 0x40) ? 1 : 0;

   device->data.btn.home  = (packet[0x0C] & 0x8)  ? 1 : 0;
}

static void hidpad_wiiupro_set_rumble(void *data,
      enum retro_rumble_effect effect, uint16_t strength)
{
   /* TODO */
}

pad_connection_interface_t pad_connection_wiiupro = {
   hidpad_wiiupro_init,
   hidpad_wiiupro_deinit,
   hidpad_wiiupro_packet_handler,
   hidpad_wiiupro_set_rumble,
   hidpad_wiiupro_get_buttons,
   hidpad_wiiupro_get_axis,
};
