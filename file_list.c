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

#include <stdlib.h>
#include <string.h>
#include "file_list.h"
#include "driver.h"
#include "compat/strcasestr.h"
#include "settings_data.h"
#include "general.h"

void file_list_push(file_list_t *list,
      const char *path, const char *label,
      unsigned type, size_t directory_ptr)
{
   if (list->size >= list->capacity)
   {
      list->capacity++;
      list->capacity *= 2;
      list->list = realloc(list->list, list->capacity * sizeof(struct item_file));
   }

   if (driver.menu_ctx && driver.menu_ctx->list_insert)
      driver.menu_ctx->list_insert(list, path, label, list->size);

   list->list[list->size].label = strdup(label);
   list->list[list->size].setting = NULL;

   rarch_setting_t *setting_data = setting_data_get_list();

   if (setting_data)
      list->list[list->size].setting = setting_data_find_setting(setting_data, label);

   if (list->list[list->size].setting)
      list->list[list->size].path = strdup(list->list[list->size].setting->short_description);
   else
      list->list[list->size].path = strdup(path);

   list->list[list->size].alt = NULL;
   list->list[list->size].type = type;
   list->list[list->size].directory_ptr = directory_ptr;
   list->size++;

}

size_t file_list_get_size(const file_list_t *list)
{
   return list->size;
}

size_t file_list_get_directory_ptr(const file_list_t *list)
{
   size_t size = file_list_get_size(list);
   return list->list[size].directory_ptr;
}

void file_list_pop(file_list_t *list, size_t *directory_ptr)
{
   if (!(list->size == 0))
   {
      if (driver.menu_ctx && driver.menu_ctx->list_delete)
         driver.menu_ctx->list_delete(list, list->size);
      --list->size;
      free(list->list[list->size].path);
      free(list->list[list->size].label);
   }

   if (directory_ptr)
      *directory_ptr = list->list[list->size].directory_ptr;

   if (driver.menu_ctx && driver.menu_ctx->list_set_selection)
      driver.menu_ctx->list_set_selection(list);
}

void file_list_free(file_list_t *list)
{
   for (size_t i = 0; i < list->size; i++)
   {
      free(list->list[i].path);
      free(list->list[i].label);
   }
   free(list->list);
   free(list);
}

void file_list_clear(file_list_t *list)
{
   for (size_t i = 0; i < list->size; i++)
   {
      free(list->list[i].path);
      free(list->list[i].label);
      free(list->list[i].alt);
   }

   if (driver.menu_ctx && driver.menu_ctx->list_clear)
      driver.menu_ctx->list_clear(list);
   list->size = 0;
}

void file_list_set_alt_at_offset(file_list_t *list, size_t index,
      const char *alt)
{
   free(list->list[index].alt);
   list->list[index].alt = strdup(alt);
}

void file_list_get_alt_at_offset(const file_list_t *list, size_t index,
      const char **alt)
{
   if (alt)
      *alt = list->list[index].alt ? list->list[index].alt : list->list[index].path;
}

static int file_list_alt_cmp(const void *a_, const void *b_)
{
   const struct item_file *a = a_;
   const struct item_file *b = b_;
   const char *cmp_a = a->alt ? a->alt : a->path;
   const char *cmp_b = b->alt ? b->alt : b->path;
   return strcasecmp(cmp_a, cmp_b);
}

void file_list_sort_on_alt(file_list_t *list)
{
   qsort(list->list, list->size, sizeof(list->list[0]), file_list_alt_cmp);
}

void file_list_get_at_offset(const file_list_t *list, size_t index,
      const char **path, unsigned *file_type, rarch_setting_t *setting)
{
   if (path)
      *path = list->list[index].path;
   if (file_type)
      *file_type = list->list[index].type;
   if (setting)
      setting = list->list[index].setting;
}

void file_list_get_last(const file_list_t *list,
      const char **path, unsigned *file_type, rarch_setting_t *setting)
{
   if (list->size)
      file_list_get_at_offset(list, list->size - 1, path, file_type, setting);
}

void *file_list_get_last_setting(const file_list_t *list, int index)
{
   rarch_setting_t *setting_data = setting_data_get_list();

   if (setting_data)
      return setting_data_find_setting(setting_data, list->list[index].label);
   return NULL;
}

bool file_list_search(const file_list_t *list, const char *needle, size_t *index)
{
   const char *alt;
   bool ret = false;

   for (size_t i = 0; i < list->size; i++)
   {
      const char *str;
      file_list_get_alt_at_offset(list, i, &alt);
      if (!alt)
         continue;

      str = strcasestr(alt, needle);
      if (str == alt) // Found match with first chars, best possible match.
      {
         *index = i;
         ret = true;
         break;
      }
      else if (str && !ret) // Found mid-string match, but try to find a match with first chars before we settle.
      {
         *index = i;
         ret = true;
      }
   }

   return ret;
}
