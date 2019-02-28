/* Katherine Control Library
 *
 * This file was created on 28.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* Unfortunately, due to multi-platform differences,
 * bitfields are declared using C macro magic. Although
 * they are obviously more elgant, Windows does not seem
 * to support them well enough.
 * 
 * Each bitfield can be pictured as a group of fields.
 * Each field has the following attributes:
 *   1. start bit - the bit where the field begins,
 *   2. mask - a bit mask which encodes the field size,
 *   3. type - a type suitable for the field value.
 * 
 * For convenience, the MASK(n) macro below defines a
 * generic integral mask of `n` bits. For instance,
 * MASK(8) will produce 0xF (represented as bits: 11111111).
 * 
 * Each bitfield's field is declared by defining three macros.
 * These directly correspond to the field's attributes listed
 * above. The naming convention for macro names is as follows:
 *   1. _BITS_{bitfield name}_{field name}_start,
 *   2. _BITS_{bitfield name}_{field name}_mask,
 *   3. _BITS_{bitfield name}_{field name}_type.
 * 
 * For convenience, the EXTRACT() macro utilizes this convention
 * to retrieve bitfield data in unified way.
 * 
 * Time for an example!
 * 
 * Consider a bitfield called "md" with a single field "header".
 * Let us also have some data called imaginatively "data". The
 * value returned by EXTRACT(data, md, header) returns the value
 * of "header" corresponding to the instance "data". Note that here
 * "data" is _not_ a pointer but an unsigned integral value large
 * enough to hold the entire bitfield (i.e. uint64_t). In this case,
 * usage of the EXTRACT macro requires the following declarations:
 *   1. _BITS_md_header_start,
 *   2. _BITS_md_header_mask,
 *   3. _BITS_md_header_type.
 */

#define MASK(n)                             ((1ul << (n)) - 1)
#define EXTRACT(val, bitfield, name)        ((_BITS_##bitfield##_##name##_type) (((val) >> (_BITS_##bitfield##_##name##_start)) & _BITS_##bitfield##_##name##_mask))

#endif