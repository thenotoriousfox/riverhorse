include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckTypeSize)

# Define this if your system has an ANSI-conforming <stddef.h> file.
check_include_file(stddef.h HAVE_STDDEF_H)

# Define this if your system has an ANSI-conforming <stdlib.h> file.
check_include_file(stdlib.h HAVE_STDLIB_H)

# Does your compiler support function prototypes?
# (If not, you also need to use ansi2knr, see install.txt)
set(HAVE_PROTOTYPES true CACHE BOOL "Does your compiler support function prototypes?")

# Does your compiler support the declaration "unsigned char" ?
# How about "unsigned short" ?
check_type_size("unsigned char" UNSIGNED_CHAR LANGUAGE C)
check_type_size("unsigned short" UNSIGNED_SHORT LANGUAGE C)

# Define "void" as "char" if your compiler doesn't know about type void.
# NOTE: be sure to define void such that "void *" represents the most general
# pointer type, e.g., that returned by malloc().
# NOT IMPLEMENTED: Modify in jconfig.h.in #

# Define "const" as empty if your compiler doesn't know the "const" keyword.
# NOT IMPLEMENTED: Modify in jconfig.h.in #

# Define this if an ordinary "char" type is unsigned.
# If you're not sure, leaving it undefined will work at some cost in speed.
# If you defined HAVE_UNSIGNED_CHAR then the speed difference is minimal.
set(CHAR_IS_UNSIGNED false CACHE BOOL "char type is unsigned")

# Define this if your system does not have an ANSI/SysV <string.h>,
# but does have a BSD-style <strings.h>.
set(NEED_BSD_STRINGS false CACHE BOOL "Use BSD <strings.h>. Use only if system lacks ANSI/SysV <strings.h>")

# Define this if your system does not provide typedef size_t in any of the
# ANSI-standard places (stddef.h, stdlib.h, or stdio.h), but places it in
# <sys/types.h> instead.
set(NEED_SYS_TYPES_H false CACHE BOOL "size_t defined in <sys/types.h>")

# For 80x86 machines, you need to define NEED_FAR_POINTERS,
# unless you are using a large-data memory model or 80386 flat-memory mode.
# On less brain-damaged CPUs this symbol must not be defined.
# (Defining this symbol causes large data structures to be referenced through
# "far" pointers and to be allocated with a special version of malloc.)
set(NEED_FAR_POINTERS false CACHE BOOL "Reference large data structures through 'far' pointers allocated with a special version of malloc")

# Define this if your linker needs global names to be unique in less
# than the first 15 characters.
set(NEED_SHORT_EXTERNAL_NAMES false CACHE BOOL "Global names must be unique in less than the first 15 characters")

# Although a real ANSI C compiler can deal perfectly well with pointers to
# unspecified structures (see "incomplete types" in the spec), a few pre-ANSI
# and pseudo-ANSI compilers get confused.  To keep one of these bozos happy,
# define INCOMPLETE_TYPES_BROKEN.  This is not recommended unless you
# actually get "missing structure definition" warnings or errors while
# compiling the JPEG code.
set(INCOMPLETE_TYPES_BROKEN false CACHE BOOL "Disable pointers to unspecified structures")

# Define "boolean" as unsigned char, not enum, on Windows systems.
# NOT IMPLEMENTED: Modify in jconfig.h.in #

# The following options affect code selection within the JPEG library,
# but they don't need to be visible to applications using the library.
# To minimize application namespace pollution, the symbols won't be
# defined unless JPEG_INTERNALS has been defined.
#

# Define this if your compiler implements ">>" on signed values as a logical
# (unsigned) shift; leave it undefined if ">>" is a signed (arithmetic) shift,
# which is the normal and rational definition.
set(RIGHT_SHIFT_IS_UNSIGNED false CACHE BOOL "Compiler implements >> on signed values as a logical (unsigned) shift")

# The remaining options do not affect the JPEG library proper,
# but only the sample applications cjpeg/djpeg (see cjpeg.c, djpeg.c).
# Other applications can ignore these.
#

mark_as_advanced(FORCE
  HAVE_PROTOTYPES
  HAVE_UNSIGNED_CHAR
  HAVE_UNSIGNED_SHORT
  CHAR_IS_UNSIGNED
  HAVE_STDDEF_H
  HAVE_STDLIB_H
  NEED_BSD_STRINGS
  NEED_SYS_TYPES_H
  NEED_FAR_POINTERS
  NEED_SHORT_EXTERNAL_NAMES
  INCOMPLETE_TYPES_BROKEN
  RIGHT_SHIFT_IS_UNSIGNED
)

configure_file(jconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/jconfig.h)
