/*
 *  color_matrix.c - Color matrix utilities
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
#include "color_matrix.h"
#include <math.h>

// Default weight values
#define Rw (0.3086)
#define Gw (0.6094)
#define Bw (0.0820)

// 3D point
typedef struct {
    float x;
    float y;
    float z;
} Point3D;

// Print color matrix M with NAME
#ifdef TEST
static void cm_print(ColorMatrix m, const char *name)
{
    int x, y;

    printf("%s = {\n", name);
    for (y = 0; y < 4; y++) {
        putchar(' ');
        for (x = 0; x < 4; x++)
            printf(" %f", m[y][x]);
        putchar('\n');
    }
    printf("}\n");
}
#endif

// Assign color matrix A to M
void cm_copy(ColorMatrix m, ColorMatrix a)
{
#if 1
    int x, y;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            m[y][x] = a[y][x];
#else
    m[0][0] = a[0][0];
    m[0][1] = a[0][1];
    m[0][2] = a[0][2];
    m[0][3] = a[0][3];

    m[1][0] = a[1][0];
    m[1][1] = a[1][1];
    m[1][2] = a[1][2];
    m[1][3] = a[1][3];

    m[2][0] = a[2][0];
    m[2][1] = a[2][1];
    m[2][2] = a[2][2];
    m[2][3] = a[2][3];

    m[3][0] = a[3][0];
    m[3][1] = a[3][1];
    m[3][2] = a[3][2];
    m[3][3] = a[3][3];
#endif
}

// Multiply color matrices A and B and set the result to M
void cm_multiply(ColorMatrix m, ColorMatrix a, ColorMatrix b)
{
    ColorMatrix t;
    int x, y;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            t[y][x] = (b[y][0] * a[0][x] +
                       b[y][1] * a[1][x] +
                       b[y][2] * a[2][x] +
                       b[y][3] * a[3][x]);

    cm_copy(m, t);
}

// Transform a 3D point using the color matrix
static void cm_transform(ColorMatrix m, const Point3D *p, Point3D *tp)
{
    tp->x = p->x * m[0][0] + p->y * m[1][0] + p->z * m[2][0] + m[3][0];
    tp->y = p->x * m[0][1] + p->y * m[1][1] + p->z * m[2][1] + m[3][1];
    tp->z = p->x * m[0][2] + p->y * m[1][2] + p->z * m[2][2] + m[3][2];
}

// Create a new color matrix with a SCALE factor
static inline void cm_set_scale(ColorMatrix m, float scale)
{
    m[0][0] = scale;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = scale;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = scale;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

// Create a new identity matrix
void cm_set_identity(ColorMatrix m)
{
    cm_set_scale(m, 1.0f);
}

// Create a new color matrix with offset R/G/B components
static void cm_set_offset(ColorMatrix m, float r, float g, float b)
{
    cm_set_identity(m);

    m[3][0] = r;
    m[3][1] = g;
    m[3][2] = b;
}

// Set color matrix components for a rotation around the x (red) axis
static void cm_set_rotation_x(ColorMatrix m, float rs, float rc)
{
    m[0][0] = 1.0f;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = rc;
    m[1][2] = rs;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = -rs;
    m[2][2] = rc;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

// Set color matrix components for a rotation around the y (green) axis
static void cm_set_rotation_y(ColorMatrix m, float rs, float rc)
{
    m[0][0] = rc;
    m[0][1] = 0.0f;
    m[0][2] = -rs;
    m[0][3] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = 1.0f;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;

    m[2][0] = rs;
    m[2][1] = 0.0f;
    m[2][2] = rc;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

// Set color matrix components for a rotation around the z (blue) axis
static void cm_set_rotation_z(ColorMatrix m, float rs, float rc)
{
    m[0][0] = rc;
    m[0][1] = rs;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;

    m[1][0] = -rs;
    m[1][1] = rc;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 1.0f;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

// Shear z using x and y
static void cm_shear_z(ColorMatrix m, float dx, float dy)
{
    ColorMatrix t;

    t[0][0] = 1.0f;
    t[0][1] = 0.0f;
    t[0][2] = dx;
    t[0][3] = 0.0f;

    t[1][0] = 0.0f;
    t[1][1] = 1.0f;
    t[1][2] = dy;
    t[1][3] = 0.0f;

    t[2][0] = 0.0f;
    t[2][1] = 0.0f;
    t[2][2] = 1.0f;
    t[2][3] = 0.0f;

    t[3][0] = 0.0f;
    t[3][1] = 0.0f;
    t[3][2] = 0.0f;
    t[3][3] = 1.0f;

    cm_multiply(m, t, m);
}

// Return BRIGHTNESS value range
void cm_get_brightness_range(float *vstart, float *vend, float *vdefault)
{
    /* Brightness value ranges from 0.0 to 10.0. A value of 1.0
       represents no modification */
    if (vstart)
        *vstart = 0.0f;
    if (vend)
        *vend = 10.0f;
    if (vdefault)
        *vdefault = 1.0f;
}

// Create a new color matrix with a specific BRIGHTNESS value
void cm_set_brightness(ColorMatrix m, float brightness)
{
    cm_set_scale(m, brightness);
}

// Return CONTRAST value range
void cm_get_contrast_range(float *vstart, float *vend, float *vdefault)
{
    /* Contrast value ranges from 0 to 10.0. A value of 1.0
       represents no modification */
    if (vstart)
        *vstart = 0.0f;
    if (vend)
        *vend = 10.0f;
    if (vdefault)
        *vdefault = 1.0f;
}

// Create a new color matrix with a specific CONTRAST value
void cm_set_contrast(ColorMatrix m, float contrast)
{
    ColorMatrix n; /* offset by -0.5f */
    ColorMatrix p; /* offset by +0.5f */
    ColorMatrix s; /* scaled by contrast */
    ColorMatrix t;

    cm_set_scale(s, contrast);
    cm_set_offset(n, -0.5f, -0.5f, -0.5f);
    cm_set_offset(p, +0.5f, +0.5f, +0.5f);
    cm_multiply(t, s, n);
    cm_multiply(m, p, t);
}

// Return SATURATION value range
void cm_get_saturation_range(float *vstart, float *vend, float *vdefault)
{
    /* Saturation value ranges from 0.0 to 10.0. A value of 1.0
       represents no modification */
    if (vstart)
        *vstart = 0.0f;
    if (vend)
        *vend = 10.0f;
    if (vdefault)
        *vdefault = 1.0f;
}

// Create a new color matrix used for SATURATION
void cm_set_saturation(ColorMatrix m, float saturation)
{
    /*
     *  +-       -+
     *  | a d g 0 |
     *  | b e h 0 |
     *  | c f i 0 |
     *  | 0 0 0 1 |
     *  +-       -+
     *
     * where
     *
     *  a = (1 - s) * Rw + s
     *  b = (1 - s) * Rw
     *  c = (1 - s) * Rw
     *  d = (1 - s) * Gw
     *  e = (1 - s) * Gw + s
     *  f = (1 - s) * Gw
     *  g = (1 - s) * Bw
     *  h = (1 - s) * Bw
     *  i = (1 - s) * Bw + s
     */
    m[0][0] = (1.0f - saturation) * Rw + saturation;
    m[0][1] = (1.0f - saturation) * Rw;
    m[0][2] = (1.0f - saturation) * Rw;
    m[0][3] = 0.0f;

    m[1][0] = (1.0f - saturation) * Gw;
    m[1][1] = (1.0f - saturation) * Gw + saturation;
    m[1][2] = (1.0f - saturation) * Gw;
    m[1][3] = 0.0f;

    m[2][0] = (1.0f - saturation) * Bw;
    m[2][1] = (1.0f - saturation) * Bw;
    m[2][2] = (1.0f - saturation) * Bw + saturation;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

// Return the HUE value range
void cm_get_hue_range(float *vstart, float *vend, float *vdefault)
{
    /* Hue value ranges from -PI to +PI. A value of 0.0 represents no
       modification */
    if (vstart)
        *vstart = -M_PI;
    if (vend)
        *vend = M_PI;
    if (vdefault)
        *vdefault = 0.0f;
}

// Create a new color matrix used for HUE rotation
void cm_set_hue(ColorMatrix m, float hue)
{
    cm_set_rotation_z(m, sin(hue), cos(hue));
}

// Rotate hue while preserving the luminance
// From <http://www.graficaobscura.com/matrix/index.html> (Paul Haeberli, 1993)
static void cm_rotate_hue(ColorMatrix m, ColorMatrix h)
{
    ColorMatrix t, xr, yr;
    float mag;
    Point3D lum, tlum;
    float xrs, xrc;
    float yrs, yrc;
    float zsx, zsy;

    /* Rotate the grey vector into positive Z */
    mag = sqrtf(2.0f);
    xrs = 1.0f / mag;
    xrc = 1.0f / mag;
    cm_set_rotation_x(xr, xrs, xrc);

    mag = sqrtf(3.0f);
    yrs = -1.0f / mag;
    yrc = sqrtf(2.0f) / mag;
    cm_set_rotation_y(yr, yrs, yrc);

    cm_set_identity(t);
    cm_multiply(t, xr, t);
    cm_multiply(t, yr, t);

    /* Shear the space to make the luminance plane horizontal */
    lum.x = Rw;
    lum.y = Gw;
    lum.z = Bw;
    cm_transform(t, &lum, &tlum);
    zsx = tlum.x / tlum.z;
    zsy = tlum.y / tlum.z;
    cm_shear_z(t, zsx, zsy);

    /* Rotate the hue */
    cm_multiply(t, h, t);

    /* Unshear the space to put the luminance plane back */
    cm_shear_z(t, -zsx, -zsy);

    /* Rotate the grey vector back into place */
    cm_set_rotation_y(yr, -yrs, yrc);
    cm_set_rotation_x(xr, -xrs, xrc);

    cm_multiply(t, yr, t);
    cm_multiply(t, xr, t);
    cm_multiply(m, t, m);
}

// Create composite matrix from brightness/contrast/saturation/hue matrices
void cm_composite(
    ColorMatrix m,
    ColorMatrix b,
    ColorMatrix c,
    ColorMatrix s,
    ColorMatrix h
)
{
    ColorMatrix t;

    cm_multiply(t, c, b);
    cm_multiply(m, t, s);
    cm_rotate_hue(m, h);
}

#ifdef TEST
int main(void)
{
    ColorMatrix brightness_matrix;
    cm_set_brightness(brightness_matrix, 1.0f);
    cm_print(brightness_matrix, "brightness matrix");

    ColorMatrix contrast_matrix;
    cm_set_contrast(contrast_matrix, 1.0f);
    cm_print(contrast_matrix, "contrast_matrix");

    ColorMatrix saturation_matrix;
    cm_set_saturation(saturation_matrix, 1.0f);
    cm_print(saturation_matrix, "saturation matrix");

    ColorMatrix hue_matrix;
    cm_set_hue(hue_matrix, 0.0f);
    cm_print(hue_matrix, "hue matrix");

    ColorMatrix color_matrix;
    cm_composite(
        color_matrix,
        brightness_matrix,
        contrast_matrix,
        saturation_matrix,
        hue_matrix
    );
    cm_print(color_matrix, "composite matrix");
    return 0;
}
#endif
