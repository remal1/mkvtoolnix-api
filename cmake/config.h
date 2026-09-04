#pragma once

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>

#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1

#ifndef __SIZEOF_POINTER__
#  if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
#    define __SIZEOF_POINTER__ 8
#  else
#    define __SIZEOF_POINTER__ 4
#  endif
#endif

#ifndef ICONV_CONST
#define ICONV_CONST
#endif

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#define HAVE_BOOST 1
#define HAVE_BOOST_SYSTEM 1
#define HAVE_BOOST_FILESYSTEM 1
#define HAVE_BOOST_LOCALE 1
#define HAVE_NLOHMANN_JSONCPP 1
#define HAVE_FLAC_FORMAT_H 1
#define HAVE_FLAC_DECODER_SKIP 1
#define HAVE_OGG 1
#define HAVE_VORBIS 1
#define HAVE_ZLIB 1
#define HAVE_ICONV 1
#define HAVE_QT 1

#define PACKAGE_NAME "MKVToolNix"
#define PACKAGE_STRING "MKVToolNix 101.0"
#define PACKAGE_TARNAME "mkvtoolnix"
#define PACKAGE_VERSION "101.0"
#define PACKAGE_BUGREPORT "https://codeberg.org/mbunkus/mkvtoolnix/issues"

#define MTX_LOCALE_DIR "locale"
#define MTX_PKG_DATA_DIR "data"
#define MTX_DOC_DIR "doc"
