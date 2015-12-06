/**
 * @file byte_conversion.h
 * @date Sunday, December 06, 2015 at 03:29:47 AM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * Contains some basic macros for converting between byte sizes
 *
 * @bug No known bugs.
 **/

#ifndef BYTE_CONVERSION_H_
#define BYTE_CONVERSION_H_

// Converts a byte (integral) value to megabytes (floating-point)
#define BYTE_TO_MB(size) (((double)(size)) / (1024.0 * 1024.0))

// Converts a megabyte (floating-point) value to bytes (integral)
#define MB_TO_BYTE(size) ((size_t)((size) * 1024.0 * 1024.0))

#endif /* BYTE_CONVERSION_H_ */
