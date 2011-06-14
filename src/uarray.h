/*
 *  uarray.h - Array utilities
 *
 *  xvba-video (C) 2009-2011 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef UARRAY_H
#define UARRAY_H

typedef struct _UArray UArray;
struct _UArray {
    void               *data;
    unsigned int        count;
};

typedef int (*UArrayCompareFunc)(const void *a, const void *b);

UArray *array_new(unsigned int element_size)
    attribute_hidden;

void array_free(UArray *array)
    attribute_hidden;

UArray *array_resize(UArray *array, unsigned int num_elements)
    attribute_hidden;

UArray *array_append(UArray *array, const void *data)
    attribute_hidden;

#define array_append_val(array, val) \
    array_append(array, &(val))

int array_lookup(UArray *array, const void *data, UArrayCompareFunc compare)
    attribute_hidden;

#define array_index(array, type, index) \
    *(type *)((char *)(array)->data + sizeof(type) * (index))

UArray *array_remove_at(UArray *array, int index)
    attribute_hidden;

UArray *array_replace_at(UArray *array, int index, const void *data)
    attribute_hidden;

#endif /* UARRAY_H */
