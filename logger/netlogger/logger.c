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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "../../netplay_compat.h"

#include "logger.h"

#if !defined(PC_DEVELOPMENT_IP_ADDRESS)
#error "An IP address for the PC logging server was not set in the Makefile, cannot continue."
#endif

#if !defined(PC_DEVELOPMENT_UDP_PORT)
#error "An UDP port for the PC logging server was not set in the Makefile, cannot continue."
#endif

static int g_sid;
static int sock;
static struct sockaddr_in target;
static char sendbuf[4096];

static int if_up_with(int index)
{
   (void)index;

   sock=socket(AF_INET, SOCK_DGRAM, 0);

   target.sin_family = AF_INET;
   target.sin_port = htons(PC_DEVELOPMENT_UDP_PORT);

   inet_pton(AF_INET, PC_DEVELOPMENT_IP_ADDRESS, &target.sin_addr);

   return 0;
}

static int if_down(int sid)
{
   (void)sid;
   return 0;
}

void logger_init()
{
   g_sid = if_up_with(1);
}

void logger_shutdown()
{
   if_down(g_sid);
}

void logger_send(const char *__format, ...)
{
   va_list args;

   va_start(args,__format);
   logger_send_v(__format, args);
   va_end(args);
}

void logger_send_v(const char *__format, va_list args)
{
   int len;
   vsnprintf(sendbuf,4000,__format, args);
   len = strlen(sendbuf);
   sendto(sock,sendbuf,len,MSG_DONTWAIT,(struct sockaddr*)&target,sizeof(target));
}
