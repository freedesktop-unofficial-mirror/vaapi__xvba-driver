/*
 *  uarray.c - Array utilities
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

#include "sysdeps.h"
#include "uarray.h"

typedef struct _UArrayPrivate UArrayPrivate;
struct _UArrayPrivate {
    UArray              array;
    unsigned int        count_max;
    unsigned int        element_size;
};

#define elem_offset(p,i) ((char *)(p)->array.data + (i) * (p)->element_size)
#define elem_size(p,n)   ((n) * (p)->element_size)

UArray *array_new(unsigned int element_size)
{
    UArrayPrivate *priv = malloc(sizeof(*priv));
    if (!priv)
        return NULL;

    priv->array.data    = NULL;
    priv->array.count   = 0;
    priv->count_max     = 0;
    priv->element_size  = element_size;
    return &priv->array;
}

void array_free(UArray *array)
{
    if (!array)
        return;

    free(array->data);
    free(array);
}

UArray *array_resize(UArray *array, unsigned int num_elements)
{
    UArrayPrivate * const priv = (UArrayPrivate *)array;
    void *data;

    if (num_elements <= priv->count_max)
        return array;

    num_elements += 4;
    data = realloc(array->data, elem_size(priv, num_elements));
    if (!data)
        return NULL;

    memset(
        elem_offset(priv, priv->count_max),
        0,
        elem_size(priv, (num_elements - priv->count_max))
    );

    array->data     = data;
    priv->count_max = num_elements;
    return array;
}

UArray *array_append(UArray *array, const void *data)
{
    UArrayPrivate * const priv = (UArrayPrivate *)array;

    if (!array_resize(array, array->count + 1))
        return NULL;

    memcpy(elem_offset(priv, array->count), data, elem_size(priv, 1));
    ++array->count;
    return array;
}

int array_lookup(UArray *array, const void *data, UArrayCompareFunc compare)
{
    UArrayPrivate * const priv = (UArrayPrivate *)array;
    char *m;
    unsigned int i;

    if (!array)
        return -1;

    m = array->data;
    if (compare) {
        for (i = 0; i < array->count; i++, m += priv->element_size)
            if (compare(m, data))
                return i;
    }
    else {
        for (i = 0; i < array->count; i++, m += priv->element_size)
            if (memcmp(m, data, elem_size(priv, 1)) == 0)
                return i;
    }
    return -1;
}

UArray *array_remove_at(UArray *array, int index)
{
    UArrayPrivate * const priv = (UArrayPrivate *)array;

    if (!array)
        return NULL;
    if (index < 0 || index >= array->count)
        return NULL;

    memcpy(
        elem_offset(priv, index),
        elem_offset(priv, array->count - 1),
        elem_size(priv, 1)
    );
    --array->count;
    return array;
}

UArray *array_replace_at(UArray *array, int index, const void *data)
{
    UArrayPrivate * const priv = (UArrayPrivate *)array;

    if (!array)
        return NULL;
    if (index < 0 || index >= array->count)
        return NULL;

    memcpy(elem_offset(priv, index), data, elem_size(priv, 1));
    return array;
}
