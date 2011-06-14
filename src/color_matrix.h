/*
 *  color_matrix.h - Color matrix utilities
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

#ifndef COLOR_MATRIX_H
#define COLOR_MATRIX_H

typedef float ColorMatrix[4][4];

// Assign color matrix A to M
void cm_copy(ColorMatrix m, ColorMatrix a)
    attribute_hidden;

// Multiply color matrices A and B and set the result to M
void cm_multiply(ColorMatrix m, ColorMatrix a, ColorMatrix b)
    attribute_hidden;

// Create a new identity matrix
void cm_set_identity(ColorMatrix m)
    attribute_hidden;

// Return CONTRAST value range
void cm_get_brightness_range(float *vstart, float *vend, float *vdefault)
    attribute_hidden;

// Create a new color matrix with a specific BRIGHTNESS value
void cm_set_brightness(ColorMatrix m, float brightness)
    attribute_hidden;

// Return CONTRAST value range
void cm_get_contrast_range(float *vstart, float *vend, float *vdefault)
    attribute_hidden;

// Create a new color matrix with a specific CONTRAST value
void cm_set_contrast(ColorMatrix m, float contrast)
    attribute_hidden;

// Return SATURATION value range
void cm_get_saturation_range(float *vstart, float *vend, float *vdefault)
    attribute_hidden;

// Create a new color matrix used for SATURATION
void cm_set_saturation(ColorMatrix m, float saturation)
    attribute_hidden;

// Return the HUE value range
void cm_get_hue_range(float *vstart, float *vend, float *vdefault)
    attribute_hidden;

// Create a new color matrix used for HUE rotation
void cm_set_hue(ColorMatrix m, float hue)
    attribute_hidden;

// Create composite matrix from brightness/contrast/saturation/hue matrices
void cm_composite(
    ColorMatrix m,
    ColorMatrix b,
    ColorMatrix c,
    ColorMatrix s,
    ColorMatrix h
) attribute_hidden;

#endif /* COLOR_MATRIX_H */
