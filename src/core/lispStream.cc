/*
    File: lispStream.cc
*/

/*
Copyright (c) 2014, Christian E. Schafmeister

CLASP is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

See directory 'clasp/licenses' for full details.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.rg

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
/* -^- */
// #define DEBUG_CURSOR 1

/*
  Originally from ECL file.d -- File interface.

    Copyright (c) 1984, Taiichi Yuasa and Masami Hagiya.
    Copyright (c) 1990, Giuseppe Attardi.
    Copyright (c) 2001, Juan Jose Garcia Ripoll.

    ECL is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    See file '../Copyright' for full details.

    Heavily modified by Christian Schafmeister 2014

    Converted to C++ virtual methods by Tarn W. Burton in Dec 2023
*/

#define DEBUG_DENSE 0

// #define DEBUG_LEVEL_FULL
#include <stdio.h>
#include <bitset>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <clasp/core/foundation.h>
#include <clasp/core/common.h>
#include <clasp/core/fileSystem.h>
#include <clasp/core/lispStream.h>
#include <clasp/core/array.h>
#include <clasp/core/symbolTable.h>
#include <clasp/core/sourceFileInfo.h>
#include <clasp/core/symbolTable.h>
#include <clasp/core/corePackage.h>
#include <clasp/core/readtable.h>
#include <clasp/core/lispDefinitions.h>
#include <clasp/core/instance.h>
#include <clasp/core/hashTable.h>
#include <clasp/core/pathname.h>
#include <clasp/core/primitives.h>
#include <clasp/core/multipleValues.h>
#include <clasp/core/evaluator.h>
#include <clasp/core/lispList.h>
#include <clasp/core/array.h>
#include <clasp/core/designators.h>
#include <clasp/core/unixfsys.h>
#include <clasp/core/lispReader.h>
#include <clasp/core/sequence.h>
#include <clasp/core/fileSystem.h>
#include <clasp/core/wrappers.h>
#include <clasp/core/bits.h>

namespace core {

gctools::Fixnum clasp_normalize_stream_element_type(T_sp element_type);

std::string string_mode(int st_mode) {
  stringstream ss;
  if (st_mode & S_IRWXU)
    ss << "/user-";
  if (st_mode & S_IRUSR)
    ss << "r";
  if (st_mode & S_IWUSR)
    ss << "w";
  if (st_mode & S_IXUSR)
    ss << "x";
  if (st_mode & S_IRWXG)
    ss << "/group-";
  if (st_mode & S_IRGRP)
    ss << "r";
  if (st_mode & S_IWGRP)
    ss << "w";
  if (st_mode & S_IXGRP)
    ss << "x";
  if (st_mode & S_IRWXO)
    ss << "/other-";
  if (st_mode & S_IROTH)
    ss << "r";
  if (st_mode & S_IWOTH)
    ss << "w";
  if (st_mode & S_IXOTH)
    ss << "x";
  return ss.str();
}

Fixnum StringFillp(String_sp s) {
  ASSERT(core__non_simple_stringp(s));
  if (!s->arrayHasFillPointerP()) {
    SIMPLE_ERROR("The vector does not have a fill pointer");
  }
  return s->fillPointer();
}

void SetStringFillp(String_sp s, Fixnum fp) {
  ASSERT(core__non_simple_stringp(s));
  if (!s->arrayHasFillPointerP()) {
    SIMPLE_ERROR("The vector does not have a fill pointer");
  }
  s->fillPointerSet(fp);
}

DOCGROUP(clasp);
CL_DEFUN StringOutputStream_sp core__thread_local_write_to_string_output_stream() { return my_thread->_WriteToStringOutputStream; }

DOCGROUP(clasp);
CL_UNWIND_COOP(true)
CL_DEFUN String_sp core__get_thread_local_write_to_string_output_stream_string(StringOutputStream_sp my_stream) {
  // This is like get-string-output-stream-string but it checks the size of the
  // buffer string and if it is too large it knocks it down to STRING_OUTPUT_STREAM_DEFAULT_SIZE characters
  String_sp result = gc::As_unsafe<String_sp>(cl__copy_seq(my_stream->_contents));
  if (my_stream->_contents->length() > 1024) {
#ifdef CLASP_UNICODE
    my_stream->_contents = StrWNs_O::createBufferString(STRING_OUTPUT_STREAM_DEFAULT_SIZE);
#else
    my_stream->_contents = Str8Ns_O::createBufferString(STRING_OUTPUT_STREAM_DEFAULT_SIZE);
#endif
  } else {
    SetStringFillp(my_stream->_contents, core::make_fixnum(0));
  }
  my_stream->_output_column = 0;
  return result;
}

void StreamCursor::advanceLineNumber(T_sp strm, claspCharacter c, int num) {
  this->_prev_line_number = this->_line_number;
  this->_prev_column = this->_column;
  this->_line_number += num;
  this->_column = 0;
#ifdef DEBUG_CURSOR
  if (core::_sym_STARdebugMonitorSTAR->symbolValue().notnilp()) {
    printf("%s:%d stream=%s advanceLineNumber=%c/%d  ln/col=%lld/%d\n", __FILE__, __LINE__, stream_pathname(strm)->get().c_str(), c,
           c, this->_line_number, this->_column);
  }
#endif
}

void StreamCursor::advanceColumn(T_sp strm, claspCharacter c, int num) {
  this->_prev_line_number = this->_line_number;
  this->_prev_column = this->_column;
  this->_column++;
#ifdef DEBUG_CURSOR
  if (core::_sym_STARdebugMonitorSTAR->symbolValue().notnilp()) {
    printf("%s:%d stream=%s advanceColumn=%c/%d  ln/col=%lld/%d\n", __FILE__, __LINE__, stream_pathname(strm)->get().c_str(), c, c,
           this->_line_number, this->_column);
  }
#endif
}

void StreamCursor::backup(T_sp strm, claspCharacter c) {
  this->_line_number = this->_prev_line_number;
  this->_column = this->_prev_column;
#ifdef DEBUG_CURSOR
  if (core::_sym_STARdebugMonitorSTAR->symbolValue().notnilp()) {
    printf("%s:%d stream=%s backup=%c/%d ln/col=%lld/%d\n", __FILE__, __LINE__, stream_pathname(strm)->get().c_str(), c, c,
           this->_line_number, this->_column);
  }
#endif
}

/* Maximum number of bytes required to encode a character.
 * This currently corresponds to (4 + 2) for the ISO-2022-JP-* encodings
 * with 4 being the charset prefix, 2 for the character.
 */
#define ENCODING_BUFFER_MAX_SIZE 6
/* Size of the encoding buffer for vectors */
#define VECTOR_ENCODING_BUFFER_SIZE 2048

/*
 * Byte operations based on octet operators.
 */

T_sp FileStream_O::read_byte_unsigned8() {
  unsigned char c;
  if (read_byte8(&c, 1) < 1) {
    return nil<T_O>();
  }
  return make_fixnum(c);
}

void FileStream_O::write_byte_unsigned8(T_sp byte) {
  unsigned char c = clasp_to_uint8_t(byte);
  write_byte8(&c, 1);
}

T_sp FileStream_O::read_byte_signed8() {
  signed char c;
  if (read_byte8((unsigned char*)&c, 1) < 1)
    return nil<T_O>();
  return make_fixnum(c);
}

void FileStream_O::write_byte_signed8(T_sp byte) {
  signed char c = clasp_to_int8_t(byte);
  write_byte8((unsigned char*)&c, 1);
}

// Max number of bits we can read without bignums; 64 for 64-bit machines
// see clasp_normalize_stream_element_type below
#define BYTE_STREAM_FAST_MAX_BITS 64

T_sp FileStream_O::read_byte_le() {
  cl_index b, bs = _byte_size / 8;
  unsigned char bytes[bs];
  read_byte8(bytes, bs);
  uint64_t result = 0;
  for (b = 0; b < bs; ++b)
    result |= (uint64_t)(bytes[b]) << (b * 8);
  if (_flags & CLASP_STREAM_SIGNED_BYTES) {
    // If the most significant bit (sign bit) is set, negate.
    // signmask is the MSB and all higher bits
    // (for when bs < 8. uint64_t is still 8 bytes)
    uint64_t signmask = ~((1 << (bs * 8 - 1)) - 1);
    if (result & signmask) {
      uint64_t uresult = ~(result | signmask) + 1;
      int64_t sresult = -uresult;
      return Integer_O::create(sresult);
    } else
      return Integer_O::create(result);
  } else
    return Integer_O::create(result);
}

void FileStream_O::write_byte_le(T_sp c) {
  cl_index b, bs = _byte_size / 8;
  unsigned char bytes[bs];
  uint64_t word;
  if (_flags & CLASP_STREAM_SIGNED_BYTES)
    // C++ defines signed to unsigned conversion to work mod 2^nbits,
    // which we leverage here.
    word = clasp_to_integral<int64_t>(c);
  else
    word = clasp_to_integral<uint64_t>(c);
  for (b = 0; b < bs; ++b) {
    bytes[b] = word & 0xFF;
    word >>= 8;
  }
  write_byte8(bytes, bs);
}

T_sp FileStream_O::read_byte_short() {
  cl_index b, rb, bs = _byte_size / 8;
  unsigned char bytes[bs];
  if (read_byte8(bytes, bs) < bs)
    return nil<T_O>();
  uint64_t result = 0;
  for (b = 0; b < bs; ++b) {
    rb = bs - b - 1;
    result |= (uint64_t)(bytes[b]) << (rb * 8);
  }
  if (_flags & CLASP_STREAM_SIGNED_BYTES) {
    uint64_t signmask = ~((1 << (bs * 8 - 1)) - 1);
    if (result & signmask) {
      uint64_t uresult = ~(result | signmask) + 1;
      int64_t sresult = -uresult;
      return Integer_O::create(sresult);
    } else
      return Integer_O::create(result);
  } else
    return Integer_O::create(result);
}

void FileStream_O::write_byte_short(T_sp c) {
  cl_index b, rb, bs = _byte_size / 8;
  unsigned char bytes[bs];
  uint64_t word;
  if (_flags & CLASP_STREAM_SIGNED_BYTES)
    word = clasp_to_integral<int64_t>(c);
  else
    word = clasp_to_integral<uint64_t>(c);
  for (b = 0; b < bs; ++b) {
    rb = bs - b - 1;
    bytes[rb] = word & 0xFF;
    word >>= 8;
  }
  write_byte8(bytes, bs);
}

// Routines for reading and writing >64 bit integers.
// We have to use bignum arithmetic, so they're distinguished and slower.
T_sp FileStream_O::read_byte_long() {
  Integer_sp result = make_fixnum(0);
  cl_index b, rb, bs = _byte_size / 8;
  unsigned char bytes[bs];
  read_byte8(bytes, bs);
  if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
    for (b = 0; b < bs; ++b) {
      Integer_sp by = make_fixnum(bytes[b]);
      result = core__logior_2op(result, clasp_ash(by, b * 8));
    }
  } else {
    for (b = 0; b < bs; ++b) {
      Integer_sp by = make_fixnum(bytes[b]);
      rb = bs - b - 1;
      result = core__logior_2op(result, clasp_ash(by, rb * 8));
    }
  }
  if (_flags & CLASP_STREAM_SIGNED_BYTES) {
    cl_index nbits = _byte_size;
    if (cl__logbitp(make_fixnum(nbits - 1), result)) { // sign bit set
      // (dpb result (byte (* 8 bs) 0) -1)
      Integer_sp mask = cl__lognot(clasp_ash(make_fixnum(-1), nbits));
      Integer_sp a = cl__logandc2(make_fixnum(-1), mask);
      Integer_sp b = core__logand_2op(result, mask);
      return core__logior_2op(a, b);
    } else
      return result;
  } else
    return result;
}

void FileStream_O::write_byte_long(T_sp c) {
  // NOTE: This is insensitive to sign, as logand works in 2's comp already.
  cl_index b, rb, bs = _byte_size / 8;
  unsigned char bytes[bs];
  Integer_sp w = gc::As<Integer_sp>(c);
  Integer_sp mask = make_fixnum(0xFF);
  if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
    for (b = 0; b < bs; ++b) {
      bytes[b] = core__logand_2op(w, mask).unsafe_fixnum();
      w = clasp_ash(w, -8);
    }
  } else {
    for (b = 0; b < bs; ++b) {
      rb = bs - b - 1;
      bytes[rb] = core__logand_2op(w, mask).unsafe_fixnum();
      w = clasp_ash(w, -8);
    }
  }
  write_byte8(bytes, bs);
}

void FileStream_O::write_byte(T_sp c) {
  check_output();

  if (_byte_size == 8) {
    if (_flags & CLASP_STREAM_SIGNED_BYTES) {
      write_byte_signed8(c);
    } else {
      write_byte_unsigned8(c);
    }
  } else if (_byte_size > BYTE_STREAM_FAST_MAX_BITS) {
    write_byte_long(c);
  } else if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
    write_byte_le(c);
  } else {
    write_byte_short(c);
  }
}

T_sp FileStream_O::read_byte() {
  check_input();

  if (_byte_size == 8) {
    if (_flags & CLASP_STREAM_SIGNED_BYTES) {
      return read_byte_signed8();
    } else {
      return read_byte_unsigned8();
    }
  } else if (_byte_size > BYTE_STREAM_FAST_MAX_BITS) {
    return read_byte_long();
  } else if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
    return read_byte_le();
  } else {
    return read_byte_short();
  }
}

/**********************************************************************
 * CHARACTER AND EXTERNAL FORMAT SUPPORT
 */

void FileStream_O::unread_char(claspCharacter c) {
  unlikely_if(c != _last_char) unread_twice(asSmartPtr());

  unsigned char buffer[2 * ENCODING_BUFFER_MAX_SIZE];
  int ndx = 0;
  T_sp l = _byte_stack;
  gctools::Fixnum i = _last_code[0];
  if (i != EOF) {
    ndx += encode(buffer, i);
  }
  i = _last_code[1];
  if (i != EOF) {
    ndx += encode(buffer + ndx, i);
  }
  while (ndx != 0) {
    l = Cons_O::create(make_fixnum(buffer[--ndx]), l);
  }
  _byte_stack = gc::As<Cons_sp>(l);
  _last_char = EOF;
  _input_cursor.backup(asSmartPtr(), c);
}

claspCharacter FileStream_O::read_char_no_cursor() {
  unsigned char buffer[ENCODING_BUFFER_MAX_SIZE];
  claspCharacter c;
  unsigned char* buffer_pos = buffer;
  unsigned char* buffer_end = buffer;
  cl_index byte_size = _byte_size / 8;
  do {
    if (read_byte8(buffer_end, byte_size) < byte_size) {
      c = EOF;
      break;
    }
    buffer_end += byte_size;
    c = decode(&buffer_pos, buffer_end);
  } while (c == EOF && (buffer_end - buffer) < ENCODING_BUFFER_MAX_SIZE);
  unlikely_if(c == _eof_char) return EOF;
  if (c != EOF) {
    _last_char = c;
    _last_code[0] = c;
    _last_code[1] = EOF;
  }
  return c;
}

claspCharacter FileStream_O::read_char() {
  claspCharacter c = read_char_no_cursor();
  switch (_flags & CLASP_STREAM_CRLF) {
  case CLASP_STREAM_CRLF:
    if (c == CLASP_CHAR_CODE_RETURN) {
      c = read_char_no_cursor();
      if (c == CLASP_CHAR_CODE_LINEFEED) {
        _last_code[0] = CLASP_CHAR_CODE_RETURN;
        _last_code[1] = c;
        c = CLASP_CHAR_CODE_NEWLINE;
      } else {
        unread_char(c);
        c = CLASP_CHAR_CODE_RETURN;
        _last_code[0] = c;
        _last_code[1] = EOF;
      }
      _last_char = c;
      _input_cursor.advanceLineNumber(asSmartPtr(), c);
    } else {
      _input_cursor.advanceColumn(asSmartPtr(), c);
    }
    break;
  case CLASP_STREAM_CR:
    if (c == CLASP_CHAR_CODE_RETURN) {
      c = CLASP_CHAR_CODE_NEWLINE;
      _last_char = c;
      _input_cursor.advanceLineNumber(asSmartPtr(), c);
    } else {
      _input_cursor.advanceColumn(asSmartPtr(), c);
    }
    break;
  default:
    _input_cursor.advanceForChar(asSmartPtr(), c, _last_char);
    break;
  }
  return c;
}

void AnsiStream_O::update_column(claspCharacter c) {
  if (c == '\n')
    _output_column = 0;
  else if (c == '\t')
    _output_column = (_output_column & ~((size_t)07)) + 8;
  else
    _output_column++;
}

claspCharacter FileStream_O::write_char(claspCharacter c) {
  check_output();

  unsigned char buffer[ENCODING_BUFFER_MAX_SIZE];
  claspCharacter nbytes;

  if ((c == CLASP_CHAR_CODE_NEWLINE) && (_flags & CLASP_STREAM_CR)) {
    nbytes = encode(buffer, CLASP_CHAR_CODE_RETURN);
    write_byte8(buffer, nbytes);

    if (_flags & CLASP_STREAM_LF) {
      nbytes = encode(buffer, CLASP_CHAR_CODE_LINEFEED);
      write_byte8(buffer, nbytes);
    }
  } else {
    nbytes = encode(buffer, c);
    write_byte8(buffer, nbytes);
  }

  update_column(c);

  return c;
}

/*
 * If we use Unicode, this is LATIN-1, ISO-8859-1, that is the 256
 * lowest codes of Unicode. Otherwise, we simply assume the file and
 * the strings use the same format.
 */

claspCharacter FileStream_O::decode(unsigned char** buffer, unsigned char* buffer_end) {
  switch (_flags & (CLASP_STREAM_FORMAT | CLASP_STREAM_LITTLE_ENDIAN)) {
#ifdef CLASP_UNICODE
  case CLASP_STREAM_UTF_8:
    return decode_utf_8(buffer, buffer_end);
  case CLASP_STREAM_UCS_2:
    return decode_ucs_2(buffer, buffer_end);
  case CLASP_STREAM_UCS_2BE:
    return decode_ucs_2be(buffer, buffer_end);
  case CLASP_STREAM_UCS_2LE:
    return decode_ucs_2le(buffer, buffer_end);
  case CLASP_STREAM_UCS_4:
    return decode_ucs_4(buffer, buffer_end);
  case CLASP_STREAM_UCS_4BE:
    return decode_ucs_4be(buffer, buffer_end);
  case CLASP_STREAM_UCS_4LE:
    return decode_ucs_4le(buffer, buffer_end);
  case CLASP_STREAM_US_ASCII:
    return decode_ascii(buffer, buffer_end);
  case CLASP_STREAM_USER_FORMAT:
    return decode_user(buffer, buffer_end);
  case CLASP_STREAM_USER_MULTISTATE_FORMAT:
    return decode_user_multistate(buffer, buffer_end);
#endif
  default:
    return decode_passthrough(buffer, buffer_end);
  }
}

int FileStream_O::encode(unsigned char* buffer, claspCharacter c) {
  switch (_flags & (CLASP_STREAM_FORMAT | CLASP_STREAM_LITTLE_ENDIAN)) {
#ifdef CLASP_UNICODE
  case CLASP_STREAM_UTF_8:
    return encode_utf_8(buffer, c);
  case CLASP_STREAM_UCS_2:
    return encode_ucs_2(buffer, c);
  case CLASP_STREAM_UCS_2BE:
    return encode_ucs_2be(buffer, c);
  case CLASP_STREAM_UCS_2LE:
    return encode_ucs_2le(buffer, c);
  case CLASP_STREAM_UCS_4:
    return encode_ucs_4(buffer, c);
  case CLASP_STREAM_UCS_4BE:
    return encode_ucs_4be(buffer, c);
  case CLASP_STREAM_UCS_4LE:
    return encode_ucs_4le(buffer, c);
  case CLASP_STREAM_US_ASCII:
    return encode_ascii(buffer, c);
  case CLASP_STREAM_USER_FORMAT:
    return encode_user(buffer, c);
  case CLASP_STREAM_USER_MULTISTATE_FORMAT:
    return encode_user_multistate(buffer, c);
#endif
  default:
    return encode_passthrough(buffer, c);
  }
}

claspCharacter FileStream_O::decode_passthrough(unsigned char** buffer, unsigned char* buffer_end) {
  if (*buffer >= buffer_end)
    return EOF;
  else
    return *((*buffer)++);
}

int FileStream_O::encode_passthrough(unsigned char* buffer, claspCharacter c) {
#ifdef CLASP_UNICODE
  unlikely_if(c > 0xFF) return encoding_error(asSmartPtr(), buffer, c);
#endif
  buffer[0] = c;
  return 1;
}

#ifdef CLASP_UNICODE
/*
 * US ASCII, that is the 128 (0-127) lowest codes of Unicode
 */

claspCharacter FileStream_O::decode_ascii(unsigned char** buffer, unsigned char* buffer_end) {
  if (*buffer >= buffer_end)
    return EOF;
  if (**buffer > 127) {
    return decoding_error(asSmartPtr(), buffer, 1, buffer_end);
  } else {
    return *((*buffer)++);
  }
}

int FileStream_O::encode_ascii(unsigned char* buffer, claspCharacter c) {
  unlikely_if(c > 127) return encoding_error(asSmartPtr(), buffer, c);
  buffer[0] = c;
  return 1;
}

/*
 * UCS-4 BIG ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_4be(unsigned char** buffer, unsigned char* buffer_end) {
  claspCharacter aux;
  if ((*buffer) + 3 >= buffer_end)
    return EOF;
  aux = (*buffer)[3] + ((*buffer)[2] << 8) + ((*buffer)[1] << 16) + ((*buffer)[0] << 24);
  *buffer += 4;
  return aux;
}

int FileStream_O::encode_ucs_4be(unsigned char* buffer, claspCharacter c) {
  buffer[3] = c & 0xFF;
  c >>= 8;
  buffer[2] = c & 0xFF;
  c >>= 8;
  buffer[1] = c & 0xFF;
  c >>= 8;
  buffer[0] = c;
  return 4;
}

/*
 * UCS-4 LITTLE ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_4le(unsigned char** buffer, unsigned char* buffer_end) {
  claspCharacter aux;
  if ((*buffer) + 3 >= buffer_end)
    return EOF;
  aux = (*buffer)[0] + ((*buffer)[1] << 8) + ((*buffer)[2] << 16) + ((*buffer)[3] << 24);
  *buffer += 4;
  return aux;
}

int FileStream_O::encode_ucs_4le(unsigned char* buffer, claspCharacter c) {
  buffer[0] = c & 0xFF;
  c >>= 8;
  buffer[1] = c & 0xFF;
  c >>= 8;
  buffer[2] = c & 0xFF;
  c >>= 8;
  buffer[3] = c;
  return 4;
}

/*
 * UCS-4 BOM ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_4(unsigned char** buffer, unsigned char* buffer_end) {
  gctools::Fixnum c = decode_ucs_4be(buffer, buffer_end);

  if (c == 0xFFFE0000) {
    _flags |= CLASP_STREAM_UCS_4LE;
    return decode_ucs_4le(buffer, buffer_end);
  }

  _flags |= CLASP_STREAM_UCS_4BE;

  return (c == 0xFEFF) ? decode_ucs_4be(buffer, buffer_end) : c;
}

int FileStream_O::encode_ucs_4(unsigned char* buffer, claspCharacter c) {
  _flags |= CLASP_STREAM_UCS_4BE;
  buffer[0] = buffer[1] = 0;
  buffer[2] = 0xFE;
  buffer[3] = 0xFF;
  return 4 + encode_ucs_4be(buffer + 4, c);
}

/*
 * UTF-16 BIG ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_2be(unsigned char** buffer, unsigned char* buffer_end) {
  if ((*buffer) + 1 >= buffer_end) {
    return EOF;
  }

  claspCharacter c = ((claspCharacter)(*buffer)[0] << 8) | (*buffer)[1];
  if (((*buffer)[0] & 0xFC) == 0xD8) {
    if ((*buffer) + 3 >= buffer_end) {
      return EOF;
    } else {
      claspCharacter aux;
      if (((*buffer)[3] & 0xFC) != 0xDC) {
        return decoding_error(asSmartPtr(), buffer, 4, buffer_end);
      }
      aux = ((claspCharacter)(*buffer)[2] << 8) | (*buffer)[3];
      *buffer += 4;
      return ((c & 0x3FFF) << 10) + (aux & 0x3FFF) + 0x10000;
    }
  }
  *buffer += 2;
  return c;
}

int FileStream_O::encode_ucs_2be(unsigned char* buffer, claspCharacter c) {
  if (c >= 0x10000) {
    c -= 0x10000;
    encode_ucs_2be(buffer, (c >> 10) | 0xD800);
    encode_ucs_2be(buffer + 2, (c & 0x3FFF) | 0xDC00);
    return 4;
  }

  buffer[1] = c & 0xFF;
  c >>= 8;
  buffer[0] = c;
  return 2;
}

/*
 * UTF-16 LITTLE ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_2le(unsigned char** buffer, unsigned char* buffer_end) {
  if ((*buffer) + 1 >= buffer_end) {
    return EOF;
  } else {
    claspCharacter c = ((claspCharacter)(*buffer)[1] << 8) | (*buffer)[0];
    if (((*buffer)[1] & 0xFC) == 0xD8) {
      if ((*buffer) + 3 >= buffer_end) {
        return EOF;
      } else {
        claspCharacter aux;
        if (((*buffer)[3] & 0xFC) != 0xDC) {
          return decoding_error(asSmartPtr(), buffer, 4, buffer_end);
        }
        aux = ((claspCharacter)(*buffer)[3] << 8) | (*buffer)[2];
        *buffer += 4;
        return ((c & 0x3FFF) << 10) + (aux & 0x3FFF) + 0x10000;
      }
    }
    *buffer += 2;
    return c;
  }
}

int FileStream_O::encode_ucs_2le(unsigned char* buffer, claspCharacter c) {
  if (c >= 0x10000) {
    c -= 0x10000;
    encode_ucs_2le(buffer, (c >> 10) | 0xD8000);
    encode_ucs_2le(buffer + 2, (c & 0x3FFF) | 0xD800);
    return 4;
  } else {
    buffer[0] = c & 0xFF;
    c >>= 8;
    buffer[1] = c & 0xFF;
    return 2;
  }
}

/*
 * UTF-16 BOM ENDIAN
 */

claspCharacter FileStream_O::decode_ucs_2(unsigned char** buffer, unsigned char* buffer_end) {
  claspCharacter c = decode_ucs_2be(buffer, buffer_end);

  if (c == 0xFFFE) {
    _flags |= CLASP_STREAM_UCS_2LE;
    return decode_ucs_2le(buffer, buffer_end);
  }

  _flags |= CLASP_STREAM_UCS_2BE;

  return (c == 0xFEFF) ? decode_ucs_2be(buffer, buffer_end) : c;
}

int FileStream_O::encode_ucs_2(unsigned char* buffer, claspCharacter c) {
  _flags |= CLASP_STREAM_UCS_2BE;
  buffer[0] = 0xFE;
  buffer[1] = 0xFF;
  return 2 + encode_ucs_2be(buffer + 2, c);
}

/*
 * USER DEFINED ENCODINGS. SIMPLE CASE.
 */

claspCharacter FileStream_O::decode_user(unsigned char** buffer, unsigned char* buffer_end) {
  if (*buffer >= buffer_end)
    return EOF;

  T_sp character = clasp_gethash_safe(clasp_make_fixnum((*buffer)[0]), _format_table, nil<T_O>());
  unlikely_if(character.nilp()) { return decoding_error(asSmartPtr(), buffer, 1, buffer_end); }
  if (character == _lisp->_true()) {
    if ((*buffer) + 1 >= buffer_end) {
      return EOF;
    } else {
      gctools::Fixnum byte = ((*buffer)[0] << 8) + (*buffer)[1];
      character = clasp_gethash_safe(clasp_make_fixnum(byte), _format_table, nil<T_O>());
      unlikely_if(character.nilp()) { return decoding_error(asSmartPtr(), buffer, 2, buffer_end); }
    }
  }
  return character.unsafe_character();
}

int FileStream_O::encode_user(unsigned char* buffer, claspCharacter c) {
  T_sp byte = clasp_gethash_safe(clasp_make_character(c), _format_table, nil<T_O>());
  if (byte.nilp()) {
    return encoding_error(asSmartPtr(), buffer, c);
  } else {
    gctools::Fixnum code = byte.unsafe_fixnum();
    if (code > 0xFF) {
      buffer[1] = code & 0xFF;
      code >>= 8;
      buffer[0] = code;
      return 2;
    } else {
      buffer[0] = code;
      return 1;
    }
  }
}

/*
 * USER DEFINED ENCODINGS. SIMPLE CASE.
 */

claspCharacter FileStream_O::decode_user_multistate(unsigned char** buffer, unsigned char* buffer_end) {
  T_sp table_list = _format_table;
  T_sp table = oCar(table_list);
  T_sp character;
  gctools::Fixnum i, j;
  for (i = j = 0; i < ENCODING_BUFFER_MAX_SIZE; i++) {
    if ((*buffer) + i >= buffer_end) {
      return EOF;
    }
    j = (j << 8) | (*buffer)[i];
    character = clasp_gethash_safe(clasp_make_fixnum(j), table, nil<T_O>());
    if (character.characterp()) {
      return character.unsafe_character();
    }
    unlikely_if(character.nilp()) { return decoding_error(asSmartPtr(), buffer, i, buffer_end); }
    if (character == _lisp->_true()) {
      /* Need more characters */
      i++;
      continue;
    }
    if (character.consp()) {
      /* Changed the state. */
      _format_table = table_list = character;
      table = oCar(table_list);
      i = j = 0;
      continue;
    }
    break;
  }
  FEerror("Internal error in decoder table.", 0);
  UNREACHABLE();
}

int FileStream_O::encode_user_multistate(unsigned char* buffer, claspCharacter c) {
  T_sp table_list = _format_table;
  T_sp p = table_list;
  do {
    T_sp table = oCar(p);
    T_sp byte = clasp_gethash_safe(clasp_make_character(c), table, nil<T_O>());
    if (!byte.nilp()) {
      gctools::Fixnum code = byte.unsafe_fixnum();
      claspCharacter n = 0;
      if (p != table_list) {
        /* Must output a escape sequence */
        T_sp x = clasp_gethash_safe(_lisp->_true(), table, nil<T_O>());
        while (!x.nilp()) {
          buffer[0] = (oCar(x)).unsafe_fixnum();
          buffer++;
          x = oCdr(x);
          n++;
        }
        _format_table = p;
      }
      if (code > 0xFF) {
        buffer[1] = code & 0xFF;
        code >>= 8;
        buffer[0] = code;
        return n + 2;
      } else {
        buffer[0] = code;
        return n + 1;
      }
    }
    p = oCdr(p);
  } while (p != table_list);
  /* Exhausted all lists */
  return encoding_error(asSmartPtr(), buffer, c);
}

/*
 * UTF-8
 */

claspCharacter FileStream_O::decode_utf_8(unsigned char** buffer, unsigned char* buffer_end) {
  /* In understanding this code:
   * 0x8 = 1000, 0xC = 1100, 0xE = 1110, 0xF = 1111
   * 0x1 = 0001, 0x3 = 0011, 0x7 = 0111, 0xF = 1111
   */
  claspCharacter cum = 0;
  int nbytes, i;
  unsigned char aux;
  if (*buffer >= buffer_end)
    return EOF;
  aux = (*buffer)[0];
  if ((aux & 0x80) == 0) {
    (*buffer)++;
    return aux;
  }
  unlikely_if((aux & 0x40) == 0) return decoding_error(asSmartPtr(), buffer, 1, buffer_end);
  if ((aux & 0x20) == 0) {
    cum = aux & 0x1F;
    nbytes = 1;
  } else if ((aux & 0x10) == 0) {
    cum = aux & 0x0F;
    nbytes = 2;
  } else if ((aux & 0x08) == 0) {
    cum = aux & 0x07;
    nbytes = 3;
  } else {
    return decoding_error(asSmartPtr(), buffer, 1, buffer_end);
  }
  if ((*buffer) + nbytes >= buffer_end)
    return EOF;
  for (i = 1; i <= nbytes; i++) {
    unsigned char c = (*buffer)[i];
    unlikely_if((c & 0xC0) != 0x80) { return decoding_error(asSmartPtr(), buffer, nbytes + 1, buffer_end); }
    cum = (cum << 6) | (c & 0x3F);
    unlikely_if(cum == 0) { return decoding_error(asSmartPtr(), buffer, nbytes + 1, buffer_end); }
  }
  if (cum >= 0xd800) {
    unlikely_if(cum <= 0xdfff) { return decoding_error(asSmartPtr(), buffer, nbytes + 1, buffer_end); }
    unlikely_if(cum >= 0xFFFE && cum <= 0xFFFF) { return decoding_error(asSmartPtr(), buffer, nbytes + 1, buffer_end); }
  }
  *buffer += nbytes + 1;
  return cum;
}

int FileStream_O::encode_utf_8(unsigned char* buffer, claspCharacter c) {
  int nbytes = 0;
  if (c < 0) {
    nbytes = 0;
  } else if (c <= 0x7F) {
    buffer[0] = c;
    nbytes = 1;
  } else if (c <= 0x7ff) {
    buffer[1] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[0] = c | 0xC0;
    /*printf("\n; %04x ;: %04x :: %04x :\n", c_orig, buffer[0], buffer[1]);*/
    nbytes = 2;
  } else if (c <= 0xFFFF) {
    buffer[2] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[1] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[0] = c | 0xE0;
    nbytes = 3;
  } else if (c <= 0x1FFFFFL) {
    buffer[3] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[2] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[1] = (c & 0x3f) | 0x80;
    c >>= 6;
    buffer[0] = c | 0xF0;
    nbytes = 4;
  }
  return nbytes;
}
#endif

cl_index FileStream_O::compute_char_size(claspCharacter c) {
  // TODO  Make this work with full characters
  unsigned char buffer[5];
  int l = 0;
  if (c == CLASP_CHAR_CODE_NEWLINE) {
    if (_flags & CLASP_STREAM_CR) {
      l += encode(buffer, CLASP_CHAR_CODE_RETURN);
      if (_flags & CLASP_STREAM_LF)
        l += encode(buffer, CLASP_CHAR_CODE_LINEFEED);
    } else {
      l += encode(buffer, CLASP_CHAR_CODE_LINEFEED);
    }
  } else {
    l += encode(buffer, c);
  }
  return l;
}

T_sp FileStream_O::string_length(T_sp string) {
  int l = 0;
  if (cl__characterp(string)) {
    l = compute_char_size(string.unsafe_character());
  } else if (cl__stringp(string)) {
    Fixnum iEnd;
    String_sp sb = string.asOrNull<String_O>();
    if (sb && (sb->arrayHasFillPointerP()))
      iEnd = StringFillp(sb);
    else
      iEnd = cl__length(sb);
    for (int i = 0; i < iEnd; ++i) {
      l += compute_char_size(cl__char(sb, i).unsafe_character());
    }
  } else {
    ERROR_WRONG_TYPE_NTH_ARG(cl::_sym_file_string_length, 2, string, cl::_sym_string);
  }
  return clasp_make_fixnum(l);
}

T_sp FileStream_O::external_format() const { return _format; }

bool FileStream_O::input_p() const { return _mode & stream_mode_input; }

bool FileStream_O::output_p() const { return _mode & stream_mode_output; }

T_sp FileStream_O::pathname() const { return cl__parse_namestring(_filename); }

T_sp FileStream_O::truename() const { return cl__truename((_open && _temp_filename.notnilp()) ? _temp_filename : _filename); }

T_sp StringStream_O::external_format() const {
#ifdef CLASP_UNICODE
  return core__base_string_p(_contents) ? kw::_sym_latin_1 : kw::_sym_ucs_4;
#else
  return kw::_sym_passThrough;
#endif
}

/**********************************************************************
 * STRING OUTPUT STREAMS
 */

claspCharacter StringOutputStream_O::write_char(claspCharacter c) {
  update_column(c);
  _contents->vectorPushExtend(clasp_make_character(c));
  return c;
}

T_sp StringOutputStream_O::element_type() const { return _contents->element_type(); }

T_sp StringOutputStream_O::position() { return Integer_O::create((gc::Fixnum)(StringFillp(_contents))); }

T_sp StringOutputStream_O::set_position(T_sp pos) {
  Fixnum disp;
  if (pos.nilp()) {
    disp = _contents->arrayTotalSize();
  } else {
    disp = clasp_to_integral<Fixnum>(pos);
  }
  if (disp < StringFillp(_contents)) {
    SetStringFillp(_contents, disp);
  } else {
    disp -= StringFillp(_contents);
    while (disp-- > 0)
      write_char(' ');
  }
  return _lisp->_true();
}

void StringOutputStream_O::clear_output() {}

void StringOutputStream_O::finish_output() {}

void StringOutputStream_O::force_output() {}

bool StringOutputStream_O::output_p() const { return true; }

CL_LAMBDA(s);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(make_string_output_stream_from_string)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__make_string_output_stream_from_string(T_sp s) {
  StringOutputStream_sp strm = StringOutputStream_O::create();
  bool stringp = cl__stringp(s);
  unlikely_if(!stringp || !gc::As<Array_sp>(s)->arrayHasFillPointerP()) {
    FEerror("~S is not a string with a fill-pointer.", 1, s.raw_());
  }
  strm->_contents = gc::As<String_sp>(s);
  strm->_output_column = 0;
  return strm;
}

T_sp clasp_make_string_output_stream(cl_index line_length, bool extended) {
#ifdef CLASP_UNICODE
  T_sp s;
  if (extended) {
    s = StrWNs_O::createBufferString(line_length);
  } else {
    s = Str8Ns_O::createBufferString(line_length);
  }
#else
  T_sp s = Str8Ns_O::createBufferString(line_length); // clasp_alloc_adjustable_base_string(line_length);
#endif
  return core__make_string_output_stream_from_string(s);
}

CL_LAMBDA("&key (element-type 'character)");
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(makeStringOutputStream)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__make_string_output_stream(Symbol_sp elementType) {
  int extended = 0;
  if (elementType == cl::_sym_base_char) {
    (void)0;
  } else if (elementType == cl::_sym_character) {
#ifdef CLASP_UNICODE
    extended = 1;
#endif
  } else if (!T_sp(eval::funcall(cl::_sym_subtypep, elementType, cl::_sym_base_char)).nilp()) {
    (void)0;
  } else if (!T_sp(eval::funcall(cl::_sym_subtypep, elementType, cl::_sym_character)).nilp()) {
#ifdef CLASP_UNICODE
    extended = 1;
#endif
  } else {
    FEerror("In MAKE-STRING-OUTPUT-STREAM, the argument :ELEMENT-TYPE (~A) must be a subtype of character", 1, elementType.raw_());
  }
  return clasp_make_string_output_stream(STRING_OUTPUT_STREAM_DEFAULT_SIZE, extended);
}

CL_LAMBDA(strm);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(get_output_stream_string)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__get_output_stream_string(T_sp strm) {
  StringOutputStream_sp stream = strm.asOrNull<StringOutputStream_O>();
  unlikely_if(!stream) af_wrongTypeOnlyArg(__FILE__, __LINE__, cl::_sym_getOutputStreamString, strm, cl::_sym_StringStream_O);
  T_sp strng = cl__copy_seq(stream->_contents);
  SetStringFillp(stream->_contents, 0);
  return strng;
}

/**********************************************************************
 * STRING INPUT STREAMS
 */

bool StringInputStream_O::input_p() const { return true; }

claspCharacter StringInputStream_O::read_char() {
  return (_input_position >= _input_limit) ? EOF : clasp_as_claspCharacter(cl__char(_contents, _input_position++));
}

void StringInputStream_O::unread_char(claspCharacter c) {
  unlikely_if(c <= 0) unread_error(asSmartPtr());
  _input_position--;
}

claspCharacter StringInputStream_O::peek_char() {
  return (_input_position >= _input_limit) ? EOF : clasp_as_claspCharacter(cl__char(_contents, _input_position));
}

ListenResult StringInputStream_O::listen() {
  return (_input_position < _input_limit) ? listen_result_available : listen_result_eof;
}

void StringInputStream_O::clear_input() {}

T_sp StringInputStream_O::element_type() const { return _contents->element_type(); }

T_sp StringInputStream_O::position() { return Integer_O::create((gc::Fixnum)_input_position); }

T_sp StringInputStream_O::set_position(T_sp pos) {
  gctools::Fixnum disp;
  if (pos.nilp()) {
    disp = _input_limit;
  } else {
    disp = clasp_to_integral<gctools::Fixnum>(pos);
    if (disp >= _input_limit) {
      disp = _input_limit;
    }
  }
  _input_position = disp;
  return _lisp->_true();
}

T_sp clasp_make_string_input_stream(T_sp strng, cl_index istart, cl_index iend) {
  ASSERT(cl__stringp(strng));
  StringInputStream_sp strm = StringInputStream_O::create();
  strm->_contents = gc::As<String_sp>(strng);
  strm->_input_position = istart;
  strm->_input_limit = iend;
  return strm;
}

CL_LAMBDA(file_descriptor &key direction);
CL_DOCSTRING(R"dx(Create a file from a file descriptor and direction)dx");
CL_UNWIND_COOP(true);
DOCGROUP(clasp);
CL_DEFUN T_sp core__make_fd_stream(int fd, Symbol_sp direction) {
  if (direction == kw::_sym_input) {
    return IOFileStream_O::make(str_create("InputIOFileStreamFromFD"), fd, stream_mode_input);
  } else if (direction == kw::_sym_output) {
    return IOFileStream_O::make(str_create("OutputIOFileStreamFromFD"), fd, stream_mode_output);
  } else {
    SIMPLE_ERROR("Could not create IOFileStream with direction {}", _rep_(direction));
  }
}

CL_LAMBDA(strng &optional (istart 0) iend);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(make_string_input_stream)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__make_string_input_stream(String_sp strng, cl_index istart, T_sp iend) {
  ASSERT(cl__stringp(strng));
  size_t_pair p = sequenceStartEnd(cl::_sym_make_string_input_stream, strng->length(), istart, iend);
  return clasp_make_string_input_stream(strng, p.start, p.end);
}

/**********************************************************************
 * TWO WAY STREAM
 */

cl_index TwoWayStream_O::read_byte8(unsigned char* c, cl_index n) { return stream_read_byte8(_input_stream, c, n); }

/*static cl_index two_way_read_byte8(T_sp strm, unsigned char* c, cl_index n) {
  if (strm == _lisp->_Roots._TerminalIO)
    stream_force_output(TwoWayStreamOutput(_lisp->_Roots._TerminalIO));
  return stream_read_byte8(TwoWayStreamInput(strm), c, n);
}*/

cl_index TwoWayStream_O::write_byte8(unsigned char* c, cl_index n) { return stream_write_byte8(_output_stream, c, n); }

void TwoWayStream_O::write_byte(T_sp byte) { stream_write_byte(_output_stream, byte); }

T_sp TwoWayStream_O::read_byte() { return stream_read_byte(_input_stream); }

claspCharacter TwoWayStream_O::read_char() { return stream_read_char(_input_stream); }

claspCharacter TwoWayStream_O::write_char(claspCharacter c) { return stream_write_char(_output_stream, c); }

void TwoWayStream_O::unread_char(claspCharacter c) { stream_unread_char(_input_stream, c); }

claspCharacter TwoWayStream_O::peek_char() { return stream_peek_char(_input_stream); }

cl_index TwoWayStream_O::read_vector(T_sp data, cl_index start, cl_index n) {
  return stream_read_vector(_input_stream, data, start, n);
}

cl_index TwoWayStream_O::write_vector(T_sp data, cl_index start, cl_index n) {
  return stream_write_vector(_output_stream, data, start, n);
}

ListenResult TwoWayStream_O::listen() { return stream_listen(_input_stream); }

void TwoWayStream_O::clear_input() { stream_clear_input(_input_stream); }

void TwoWayStream_O::clear_output() { stream_clear_output(_output_stream); }

void TwoWayStream_O::force_output() { stream_force_output(_output_stream); }

void TwoWayStream_O::finish_output() { stream_finish_output(_output_stream); }

bool TwoWayStream_O::input_p() const { return true; }

bool TwoWayStream_O::output_p() const { return true; }

bool TwoWayStream_O::interactive_p() const { return stream_interactive_p(_input_stream); }

T_sp TwoWayStream_O::element_type() const { return stream_element_type(_input_stream); }

T_sp TwoWayStream_O::position() { return nil<T_O>(); }

int TwoWayStream_O::column() const { return stream_column(_output_stream); }

int TwoWayStream_O::set_column(int column) { return stream_set_column(_output_stream, column); }

int TwoWayStream_O::input_handle() { return stream_input_handle(_input_stream); }

int TwoWayStream_O::output_handle() { return stream_output_handle(_output_stream); }

T_sp TwoWayStream_O::close(T_sp abort) {
  if (_open) {
    _open = false;
    if (_flags & CLASP_STREAM_CLOSE_COMPONENTS) {
      stream_close(_input_stream, abort);
      stream_close(_output_stream, abort);
    }
  }
  return _lisp->_true();
}

CL_LISPIFY_NAME("cl:make-two-way-stream")
CL_LAMBDA(input-stream output-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns a two-way stream that gets its input from input-stream and
sends its output to output-stream.)dx");
DOCGROUP(clasp);
CL_DEFUN TwoWayStream_sp TwoWayStream_O::make(T_sp input_stream, T_sp output_stream) {
  check_input_stream(input_stream);
  check_output_stream(output_stream);

  TwoWayStream_sp stream = create();
  stream->_input_stream = input_stream;
  stream->_output_stream = output_stream;

  return stream;
}

CL_LISPIFY_NAME("cl:two-way-stream-input-stream")
CL_LAMBDA(two-way-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns the input stream from which two-way-stream receives input.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp TwoWayStream_O::input_stream(T_sp two_way_stream) {
  if (!two_way_stream.isA<TwoWayStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_two_way_stream_input_stream, two_way_stream, cl::_sym_TwoWayStream_O);

  return two_way_stream.as_unsafe<TwoWayStream_O>()->_input_stream;
}

CL_LISPIFY_NAME("cl:two-way-stream-output-stream")
CL_LAMBDA(two-way-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns the output stream from which two-way-stream sends output.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp TwoWayStream_O::output_stream(T_sp two_way_stream) {
  if (!two_way_stream.isA<TwoWayStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_two_way_stream_output_stream, two_way_stream, cl::_sym_TwoWayStream_O);

  return two_way_stream.as_unsafe<TwoWayStream_O>()->_output_stream;
}

/**********************************************************************
 * BROADCAST STREAM
 */

cl_index BroadcastStream_O::write_byte8(unsigned char* c, cl_index n) {
  cl_index out = n;
  for (T_sp l = _streams; !l.nilp(); l = oCdr(l)) {
    out = stream_write_byte8(oCar(l), c, n);
  }
  return out;
}

claspCharacter BroadcastStream_O::write_char(claspCharacter c) {
  for (T_sp l = _streams; !l.nilp(); l = oCdr(l)) {
    stream_write_char(oCar(l), c);
  }
  return c;
}

void BroadcastStream_O::write_byte(T_sp c) {
  T_sp l;
  for (l = _streams; !l.nilp(); l = oCdr(l)) {
    stream_write_byte(oCar(l), c);
  }
}

void BroadcastStream_O::clear_output() {
  for (T_sp l = _streams; !l.nilp(); l = oCdr(l)) {
    stream_clear_output(oCar(l));
  }
}

void BroadcastStream_O::force_output() {
  for (T_sp l = _streams; !l.nilp(); l = oCdr(l)) {
    stream_force_output(oCar(l));
  }
}

void BroadcastStream_O::finish_output() {
  for (T_sp l = _streams; !l.nilp(); l = oCdr(l)) {
    stream_finish_output(oCar(l));
  }
}

bool BroadcastStream_O::output_p() const { return true; }

T_sp BroadcastStream_O::element_type() const {
  return _streams.nilp() ? _lisp->_true() : stream_element_type(oCar(cl__last(_streams, clasp_make_fixnum(1))));
}

T_sp BroadcastStream_O::external_format() const {
  return _streams.nilp() ? (T_sp)kw::_sym_default : stream_external_format(oCar(cl__last(_streams, clasp_make_fixnum(1))));
}

T_sp BroadcastStream_O::length() {
  return _streams.nilp() ? (T_sp)clasp_make_fixnum(0) : stream_length(oCar(cl__last(_streams, clasp_make_fixnum(1))));
}

T_sp BroadcastStream_O::position() {
  return _streams.nilp() ? (T_sp)clasp_make_fixnum(0) : stream_position(oCar(cl__last(_streams, clasp_make_fixnum(1))));
}

T_sp BroadcastStream_O::set_position(T_sp pos) { return _streams.nilp() ? nil<T_O>() : stream_set_position(oCar(_streams), pos); }

T_sp BroadcastStream_O::string_length(T_sp string) {
  return _streams.nilp() ? (T_sp)clasp_make_fixnum(1)
                         : stream_string_length(oCar(cl__last(_streams, clasp_make_fixnum(1))), string);
}

int BroadcastStream_O::column() const { return _streams.nilp() ? -1 : stream_column(oCar(_streams)); }

int BroadcastStream_O::set_column(int column) {
  for (T_sp cur = _streams; cur.consp(); cur = gc::As_unsafe<Cons_sp>(cur)->cdr()) {
    stream_set_column(oCar(cur), column);
  }
  return column;
}

T_sp BroadcastStream_O::close(T_sp abort) {
  if (_open) {
    if (_flags & CLASP_STREAM_CLOSE_COMPONENTS) {
      for (T_sp head = _streams; head.notnilp() && gc::IsA<Cons_sp>(head); head = oCdr(head)) {
        stream_close(oCar(head), abort);
      }
    }
    _open = false;
  }
  return _lisp->_true();
}

CL_LISPIFY_NAME("cl:make-broadcast-stream")
CL_LAMBDA(&rest streams);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns a broadcast stream.)dx");
DOCGROUP(clasp);
CL_DEFUN BroadcastStream_sp BroadcastStream_O::make(List_sp streams) {
  for (T_sp head = streams; !head.nilp(); head = oCdr(head))
    check_output_stream(oCar(head));

  BroadcastStream_sp stream = create();
  stream->_streams = streams;

  return stream;
}

CL_LISPIFY_NAME("cl:broadcast-stream-streams")
CL_LAMBDA(broadcast-stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(Returns a list of output streams that constitute all the streams to
which the broadcast-stream is broadcasting.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp BroadcastStream_O::streams(T_sp broadcast_stream) {
  if (!broadcast_stream.isA<BroadcastStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_broadcast_stream_streams, broadcast_stream, cl::_sym_BroadcastStream_O);

  return cl__copy_list(broadcast_stream.as_unsafe<BroadcastStream_O>()->_streams);
}

/**********************************************************************
 * ECHO STREAM
 */

cl_index EchoStream_O::read_byte8(unsigned char* c, cl_index n) {
  return stream_write_byte8(_output_stream, c, stream_read_byte8(_input_stream, c, n));
}

cl_index EchoStream_O::write_byte8(unsigned char* c, cl_index n) { return stream_write_byte8(_output_stream, c, n); }

void EchoStream_O::write_byte(T_sp c) { stream_write_byte(_output_stream, c); }

T_sp EchoStream_O::read_byte() {
  T_sp out = stream_read_byte(_input_stream);
  if (!out.nilp())
    stream_write_byte(_output_stream, out);
  return out;
}

claspCharacter EchoStream_O::read_char() {
  claspCharacter c = _last_char;
  if (c == EOF) {
    c = stream_read_char(_input_stream);
    if (c != EOF)
      stream_write_char(_output_stream, c);
  } else {
    _last_char = EOF;
    stream_read_char(_input_stream);
  }
  return c;
}

claspCharacter EchoStream_O::write_char(claspCharacter c) { return stream_write_char(_output_stream, c); }

void EchoStream_O::unread_char(claspCharacter c) {
  unlikely_if(_last_char != EOF) unread_twice(asSmartPtr());
  _last_char = c;
  stream_unread_char(_input_stream, c);
}

claspCharacter EchoStream_O::peek_char() {
  claspCharacter c = _last_char;
  if (c == EOF) {
    c = stream_peek_char(_input_stream);
  }
  return c;
}

ListenResult EchoStream_O::listen() { return stream_listen(_input_stream); }

void EchoStream_O::clear_input() { stream_clear_input(_input_stream); }

void EchoStream_O::clear_output() { stream_clear_output(_output_stream); }

void EchoStream_O::force_output() { stream_force_output(_output_stream); }

void EchoStream_O::finish_output() { stream_finish_output(_output_stream); }

bool EchoStream_O::input_p() const { return true; }

bool EchoStream_O::output_p() const { return true; }

T_sp EchoStream_O::element_type() const { return stream_element_type(_input_stream); }

T_sp EchoStream_O::position() { return nil<T_O>(); }

int EchoStream_O::column() const { return stream_column(_output_stream); }

int EchoStream_O::set_column(int column) { return stream_set_column(_output_stream, column); }

int EchoStream_O::input_handle() { return stream_input_handle(_input_stream); }

int EchoStream_O::output_handle() { return stream_output_handle(_output_stream); }

T_sp EchoStream_O::close(T_sp abort) {
  if (_open) {
    if (_flags & CLASP_STREAM_CLOSE_COMPONENTS) {
      stream_close(_input_stream, abort);
      stream_close(_output_stream, abort);
    }
    _open = false;
  }
  return _lisp->_true();
}

CL_LISPIFY_NAME("cl:make-echo-stream")
CL_LAMBDA(input-stream output-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Creates and returns an echo stream that takes input from input-stream
and sends output to output-stream.)dx");
DOCGROUP(clasp);
CL_DEFUN EchoStream_sp EchoStream_O::make(T_sp input_stream, T_sp output_stream) {
  check_input_stream(input_stream);
  check_output_stream(output_stream);

  EchoStream_sp stream = create();
  stream->_input_stream = input_stream;
  stream->_output_stream = output_stream;

  return stream;
}

CL_LISPIFY_NAME("cl:echo-stream-input-stream")
CL_LAMBDA(echo-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns the input stream from which echo-stream receives input.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp EchoStream_O::input_stream(T_sp echo_stream) {
  if (!echo_stream.isA<EchoStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_echo_stream_input_stream, echo_stream, cl::_sym_EchoStream_O);

  return echo_stream.as_unsafe<EchoStream_O>()->_input_stream;
}

CL_LISPIFY_NAME("cl:echo-stream-output-stream")
CL_LAMBDA(echo-stream);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns the output stream from which echo-stream sends output.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp EchoStream_O::output_stream(T_sp echo_stream) {
  if (!echo_stream.isA<EchoStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_echo_stream_output_stream, echo_stream, cl::_sym_EchoStream_O);

  return echo_stream.as_unsafe<EchoStream_O>()->_output_stream;
}

/**********************************************************************
 * CONCATENATED STREAM
 */

cl_index ConcatenatedStream_O::read_byte8(unsigned char* c, cl_index n) {
  check_open();

  cl_index out = 0;
  while (out < n && !_streams.nilp()) {
    cl_index delta = stream_read_byte8(oCar(_streams), c + out, n - out);
    out += delta;
    if (out == n)
      break;
    _streams = oCdr(_streams);
  }
  return out;
}

T_sp ConcatenatedStream_O::read_byte() {
  check_open();

  T_sp l = _streams;
  T_sp c = nil<T_O>();
  while (!l.nilp()) {
    c = stream_read_byte(oCar(l));
    if (c != nil<T_O>())
      break;
    _streams = l = oCdr(l);
  }
  return c;
}

bool ConcatenatedStream_O::input_p() const { return true; }

// this is wrong, must be specific for concatenated streams
// should be concatenated_element_type with a proper definition for that
// ccl does more or less (stream-element-type (concatenated-stream-current-input-stream s))
T_sp ConcatenatedStream_O::element_type() const { return _streams.nilp() ? _lisp->_true() : stream_element_type(oCar(_streams)); }

claspCharacter ConcatenatedStream_O::read_char() {
  check_open();

  T_sp l = _streams;
  claspCharacter c = EOF;
  while (!l.nilp()) {
    c = stream_read_char(oCar(l));
    if (c != EOF)
      break;
    _streams = l = oCdr(l);
  }
  return c;
}

void ConcatenatedStream_O::unread_char(claspCharacter c) {
  check_open();

  unlikely_if(_streams.nilp()) unread_error(asSmartPtr());
  stream_unread_char(oCar(_streams), c);
}

ListenResult ConcatenatedStream_O::listen() {
  check_open();

  while (!_streams.nilp()) {
    ListenResult f = stream_listen(oCar(_streams));
    if (f != listen_result_eof) {
      return f;
    }
    _streams = oCdr(_streams);
  }
  return listen_result_eof;
}

void ConcatenatedStream_O::clear_input() {
  check_open();

  if (_streams.notnilp())
    stream_clear_input(oCar(_streams));
}

T_sp ConcatenatedStream_O::position() { return nil<T_O>(); }

T_sp ConcatenatedStream_O::close(T_sp abort) {
  if (_open) {
    if (_flags & CLASP_STREAM_CLOSE_COMPONENTS) {
      for (T_sp head = _streams; head.notnilp() && gc::IsA<Cons_sp>(head); head = oCdr(head)) {
        stream_close(oCar(head), abort);
      }
    }
    _open = false;
  }
  return _lisp->_true();
}

CL_LISPIFY_NAME("cl:make-concatenated-stream")
CL_LAMBDA(&rest input-streams);
CL_DECLARE();
CL_UNWIND_COOP(true);
CL_DOCSTRING(R"dx(Returns a concatenated stream that has the indicated input-streams
initially associated with it)dx");
DOCGROUP(clasp);
CL_DEFUN ConcatenatedStream_sp ConcatenatedStream_O::make(List_sp input_streams) {
  for (T_sp head = input_streams; !head.nilp(); head = oCdr(head))
    check_input_stream(oCar(head));

  ConcatenatedStream_sp stream = create();
  stream->_streams = input_streams;

  return stream;
}

CL_LISPIFY_NAME("cl:concatenated-stream-streams")
CL_LAMBDA(concatenated-stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(Returns a list of input streams that constitute the ordered set of
streams the concatenated-stream still has to read from, starting with
the current one it is reading from. The list may be empty if no more
streams remain to be read.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp ConcatenatedStream_O::streams(T_sp concatenated_stream) {
  if (!concatenated_stream.isA<ConcatenatedStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_concatenated_stream_streams, concatenated_stream, cl::_sym_ConcatenatedStream_O);

  return cl__copy_list(concatenated_stream.as_unsafe<ConcatenatedStream_O>()->_streams);
}

/**********************************************************************
 * SYNONYM STREAM
 */

cl_index SynonymStream_O::read_byte8(unsigned char* c, cl_index n) { return stream_read_byte8(stream(), c, n); }

cl_index SynonymStream_O::write_byte8(unsigned char* c, cl_index n) { return stream_write_byte8(stream(), c, n); }

void SynonymStream_O::write_byte(T_sp c) { stream_write_byte(stream(), c); }

T_sp SynonymStream_O::read_byte() { return stream_read_byte(stream()); }

claspCharacter SynonymStream_O::read_char() { return stream_read_char(stream()); }

claspCharacter SynonymStream_O::write_char(claspCharacter c) { return stream_write_char(stream(), c); }

void SynonymStream_O::unread_char(claspCharacter c) { stream_unread_char(stream(), c); }

claspCharacter SynonymStream_O::peek_char() { return stream_peek_char(stream()); }

cl_index SynonymStream_O::read_vector(T_sp data, cl_index start, cl_index n) {
  return stream_read_vector(stream(), data, start, n);
}

cl_index SynonymStream_O::write_vector(T_sp data, cl_index start, cl_index n) {
  return stream_write_vector(stream(), data, start, n);
}

ListenResult SynonymStream_O::listen() { return stream_listen(stream()); }

void SynonymStream_O::clear_input() { stream_clear_input(stream()); }

void SynonymStream_O::clear_output() { stream_clear_output(stream()); }

void SynonymStream_O::force_output() { stream_force_output(stream()); }

void SynonymStream_O::finish_output() { stream_finish_output(stream()); }

bool SynonymStream_O::input_p() const { return stream_input_p(stream()); }

bool SynonymStream_O::output_p() const { return stream_output_p(stream()); }

bool SynonymStream_O::interactive_p() const { return stream_interactive_p(stream()); }

T_sp SynonymStream_O::element_type() const { return stream_element_type(stream()); }

T_sp SynonymStream_O::external_format() const { return stream_external_format(stream()); }

T_sp SynonymStream_O::set_external_format(T_sp format) { return stream_set_external_format(stream(), format); }

T_sp SynonymStream_O::length() { return stream_length(stream()); }

T_sp SynonymStream_O::position() { return stream_position(stream()); }

T_sp SynonymStream_O::set_position(T_sp pos) { return stream_set_position(stream(), pos); }

int SynonymStream_O::column() const { return stream_column(stream()); }

int SynonymStream_O::set_column(int column) { return stream_set_column(stream(), column); }

int SynonymStream_O::input_handle() { return stream_input_handle(stream()); }

int SynonymStream_O::output_handle() { return stream_output_handle(stream()); }

T_sp SynonymStream_O::pathname() const { return stream_pathname(stream()); };

T_sp SynonymStream_O::truename() const { return stream_truename(stream()); };

CL_LISPIFY_NAME("cl:make-synonym-stream")
CL_LAMBDA(symbol);
CL_DECLARE();
CL_DOCSTRING(R"dx(Returns a synonym stream whose synonym stream symbol is symbol.)dx");
DOCGROUP(clasp);
CL_DEFUN SynonymStream_sp SynonymStream_O::make(T_sp symbol) {
  SynonymStream_sp x = create();
  x->_symbol = gc::As<Symbol_sp>(symbol);
  return x;
}

CL_LISPIFY_NAME("cl:synonym-stream-symbol")
CL_LAMBDA(s);
CL_DECLARE();
CL_DOCSTRING(R"dx(See CLHS synonym-stream-symbol)dx");
DOCGROUP(clasp);
CL_DEFUN Symbol_sp SynonymStream_O::symbol(T_sp synonym_stream) {
  if (!synonym_stream.isA<SynonymStream_O>())
    ERROR_WRONG_TYPE_ONLY_ARG(cl::_sym_synonym_stream_symbol, synonym_stream, cl::_sym_SynonymStream_O);
  return synonym_stream.as_unsafe<SynonymStream_O>()->_symbol;
}

/**********************************************************************
 * UNINTERRUPTED OPERATIONS
 */

int safe_open(const char* filename, int flags, clasp_mode_t mode) {
  const cl_env_ptr the_env = clasp_process_env();
  clasp_disable_interrupts_env(the_env);
  int output = open(filename, flags, mode);
  clasp_enable_interrupts_env(the_env);
  return output;
}

static int safe_close(int f) {
  const cl_env_ptr the_env = clasp_process_env();
  int output;
  clasp_disable_interrupts_env(the_env);
  output = close(f);
  clasp_enable_interrupts_env(the_env);
  return output;
}

static FILE* safe_fopen(const char* filename, const char* mode) {
  const cl_env_ptr the_env = clasp_process_env();
  FILE* output;
  clasp_disable_interrupts_env(the_env);
  output = fopen(filename, mode);
  clasp_enable_interrupts_env(the_env);
  return output;
}

/*
 * Return the (stdio) flags for a given mode.  Store the flags
 * to be passed to an open() syscall through *optr.
 * Return 0 on error.
 */
int sflags(const char* mode, int* optr) {
  int ret, m, o;

  switch (*mode++) {

  case 'r': /* open for reading */
    ret = 1;
    m = O_RDONLY;
    o = 0;
    break;

  case 'w': /* open for writing */
    ret = 1;
    m = O_WRONLY;
    o = O_CREAT | O_TRUNC;
    break;

  case 'a': /* open for appending */
    ret = 1;
    m = O_WRONLY;
    o = O_CREAT | O_APPEND;
    break;

  default: /* illegal mode */
    errno = EINVAL;
    return (0);
  }

  /* [rwa]\+ or [rwa]b\+ means read and write */
  if (*mode == '+' || (*mode == 'b' && mode[1] == '+')) {
    ret = 1;
    m = O_RDWR;
  }
  *optr = m | o;
  return (ret);
}

static FILE* safe_fdopen(int fildes, const char* mode) {
  const cl_env_ptr the_env = clasp_process_env();
  FILE* output;
  clasp_disable_interrupts_env(the_env);
  output = fdopen(fildes, mode);
  if (output == NULL) {
    std::string serr = strerror(errno);
    struct stat info;
    [[maybe_unused]] int fstat_error = fstat(fildes, &info);
    int flags, fdflags, tmp, oflags;
    if ((flags = sflags(mode, &oflags)) == 0)
      perror("sflags failed");
    if ((fdflags = fcntl(fildes, F_GETFL, 0)) < 0)
      perror("fcntl failed");
    tmp = fdflags & O_ACCMODE;
    if (tmp != O_RDWR && (tmp != (oflags & O_ACCMODE))) {
      printf("%s:%d fileds: %d fdflags = %d\n", __FUNCTION__, __LINE__, fildes, fdflags);
      printf("%s:%d | (tmp[%d] != O_RDWR[%d]) -> %d\n", __FUNCTION__, __LINE__, tmp, O_RDWR, (tmp != O_RDWR));
      printf("%s:%d | (tmp[%d] != (oflags[%d] & O_ACCMODE[%d])[%d]) -> %d\n", __FUNCTION__, __LINE__, tmp, oflags, O_ACCMODE,
             (oflags & O_ACCMODE), (tmp != (oflags & O_ACCMODE)));
      perror("About to signal EINVAL");
    }
    printf("%s:%d | Failed to create FILE* %p for file descriptor %d mode: %s | info.st_mode = %08x | %s\n", __FUNCTION__, __LINE__,
           output, fildes, mode, info.st_mode, serr.c_str());
    perror("In safe_fdopen");
  }
  clasp_enable_interrupts_env(the_env);
  return output;
}

static int safe_fclose(FILE* stream) {
  const cl_env_ptr the_env = clasp_process_env();
  int output;
  clasp_disable_interrupts_env(the_env);
  output = fclose(stream);
  clasp_enable_interrupts_env(the_env);
  return output;
}

/**********************************************************************
 * POSIX FILE STREAM
 */

cl_index IOFileStream_O::read_byte8(unsigned char* c, cl_index n) {
  check_input();

  if (_byte_stack.notnilp())
    return consume_byte_stack(c, n);

  gctools::Fixnum out = 0;

  clasp_disable_interrupts();
  do {
    out = read(_file_descriptor, c, sizeof(char) * n);
  } while (out < 0 && restartable_io_error("read"));
  clasp_enable_interrupts();

  return out;
}

cl_index IOFileStream_O::write_byte8(unsigned char* c, cl_index n) {
  check_output();

  if (input_p()) {
    unlikely_if(_byte_stack.notnilp()) {
      /* Try to move to the beginning of the unread characters */
      T_sp aux = stream_position(asSmartPtr());
      if (!aux.nilp())
        stream_set_position(asSmartPtr(), aux);
      _byte_stack = nil<T_O>();
    }
  }

  gctools::Fixnum out;
  clasp_disable_interrupts();
  do {
    out = write(_file_descriptor, c, sizeof(char) * n);
  } while (out < 0 && restartable_io_error("write"));
  clasp_enable_interrupts();
  return out;
}

ListenResult IOFileStream_O::listen() {
  check_input();

  if (_byte_stack.notnilp())
    return listen_result_available;
  if (_flags & CLASP_STREAM_MIGHT_SEEK) {
    cl_env_ptr the_env = clasp_process_env();
    clasp_off_t disp, onew;
    clasp_disable_interrupts_env(the_env);
    disp = lseek(_file_descriptor, 0, SEEK_CUR);
    clasp_enable_interrupts_env(the_env);
    if (disp != (clasp_off_t)-1) {
      clasp_disable_interrupts_env(the_env);
      onew = lseek(_file_descriptor, 0, SEEK_END);
      clasp_enable_interrupts_env(the_env);
      lseek(_file_descriptor, disp, SEEK_SET);
      if (onew == disp) {
        return listen_result_no_char;
      } else if (onew != (clasp_off_t)-1) {
        return listen_result_available;
      }
    }
  }
  return _fd_listen(_file_descriptor);
}

void IOFileStream_O::clear_input() {
  check_input();
  while (_fd_listen(_file_descriptor) == listen_result_available) {
    claspCharacter c = read_char();
    if (c == EOF)
      return;
  }
}

void IOFileStream_O::clear_output() { check_output(); }

void IOFileStream_O::force_output() { check_output(); }

void IOFileStream_O::finish_output() { check_output(); }

bool IOFileStream_O::interactive_p() const { return isatty(_file_descriptor); }

T_sp FileStream_O::element_type() const { return _element_type; }

T_sp IOFileStream_O::length() {
  T_sp output = clasp_file_len(_file_descriptor); // NIL or Integer_sp
  if (_byte_size != 8 && output.notnilp()) {
    Real_mv output_mv = clasp_floor2(gc::As_unsafe<Integer_sp>(output), make_fixnum(_byte_size / 8));
    // and now lets use the calculated value
    output = output_mv;
    MultipleValues& mvn = core::lisp_multipleValues();
    Fixnum_sp fn1 = gc::As<Fixnum_sp>(mvn.valueGet(1, output_mv.number_of_values()));
    unlikely_if(unbox_fixnum(fn1) != 0) { FEerror("File length is not on byte boundary", 0); }
  }
  return output;
}

T_sp IOFileStream_O::position() {
  T_sp output;
  clasp_off_t offset;

  clasp_disable_interrupts();
  offset = lseek(_file_descriptor, 0, SEEK_CUR);
  clasp_enable_interrupts();
  unlikely_if(offset < 0) io_error(asSmartPtr());
  if (sizeof(clasp_off_t) == sizeof(long)) {
    output = Integer_O::create((gctools::Fixnum)offset);
  } else {
    output = clasp_off_t_to_integer(offset);
  }
  {
    /* If there are unread octets, we return the position at which
     * these bytes begin! */
    T_sp l = _byte_stack;
    while ((l).consp()) {
      output = clasp_one_minus(gc::As<Number_sp>(output));
      l = oCdr(l);
    }
  }
  if (_byte_size != 8) {
    output = clasp_floor2(gc::As<Real_sp>(output), make_fixnum(_byte_size / 8));
  }
  return output;
}

T_sp IOFileStream_O::set_position(T_sp pos) {
  clasp_off_t disp;
  int mode;
  if (pos.nilp()) {
    disp = 0;
    mode = SEEK_END;
  } else {
    if (_byte_size != 8) {
      pos = clasp_times(gc::As<Number_sp>(pos), make_fixnum(_byte_size / 8));
    }
    disp = clasp_integer_to_off_t(pos);
    mode = SEEK_SET;
  }
  disp = lseek(_file_descriptor, disp, mode);
  return (disp == (clasp_off_t)-1) ? nil<T_O>() : _lisp->_true();
}

void FileStream_O::close_cleanup(T_sp abort) {
  if (abort.nilp()) {
    if (_temp_filename.notnilp()) {
      cl__rename_file(_temp_filename, cl__truename(_filename), kw::_sym_supersede);
    }
  } else if (_created) {
    cl__delete_file(_filename);
  } else if (_temp_filename.notnilp()) {
    cl__delete_file(_temp_filename);
  }
}

int IOFileStream_O::input_handle() { return (_mode == stream_mode_input || _mode == stream_mode_io) ? _file_descriptor : -1; }

int IOFileStream_O::output_handle() { return (_mode == stream_mode_output || _mode == stream_mode_io) ? _file_descriptor : -1; }

T_sp IOFileStream_O::close(T_sp abort) {
  if (_open) {
    int failed;
    unlikely_if(_file_descriptor == STDOUT_FILENO) FEerror("Cannot close the standard output", 0);
    unlikely_if(_file_descriptor == STDIN_FILENO) FEerror("Cannot close the standard input", 0);
    failed = safe_close(_file_descriptor);
    unlikely_if(failed < 0) cannot_close(asSmartPtr());
    _file_descriptor = -1;
    close_cleanup(abort);
    _open = false;
  }
  return _lisp->_true();
}

T_sp IOFileStream_O::make(T_sp fname, int fd, StreamMode smm, gctools::Fixnum byte_size, int flags, T_sp external_format,
                          T_sp tempName, bool created) {
  IOFileStream_sp stream = IOFileStream_O::create();
  stream->_temp_filename = tempName;
  stream->_created = created;
  stream->_mode = smm;
  stream->_open = true;
  stream->_byte_size = byte_size;
  stream->_flags = flags;
  stream->set_external_format(external_format);
  stream->_filename = fname;
  stream->_output_column = 0;
  stream->_file_descriptor = fd;
  stream->_last_op = 0;
  return stream;
}

claspCharacter FileStream_O::decode_char_from_buffer(unsigned char* buffer, unsigned char** buffer_pos, unsigned char** buffer_end,
                                                     bool seekable, cl_index min_needed_bytes) {
  bool crlf = 0;
  unsigned char* previous_buffer_pos;
  claspCharacter c;
AGAIN:
  previous_buffer_pos = *buffer_pos;
  c = decode(buffer_pos, *buffer_end);
  if (c != EOF) {
    /* Ugly handling of line breaks */
    if (crlf) {
      if (c == CLASP_CHAR_CODE_LINEFEED) {
        _last_code[1] = c;
        c = CLASP_CHAR_CODE_NEWLINE;
      } else {
        *buffer_pos = previous_buffer_pos;
        c = CLASP_CHAR_CODE_RETURN;
      }
    } else if ((_flags & CLASP_STREAM_CR) && c == CLASP_CHAR_CODE_RETURN) {
      if (_flags & CLASP_STREAM_LF) {
        _last_code[0] = c;
        crlf = 1;
        goto AGAIN;
      } else
        c = CLASP_CHAR_CODE_NEWLINE;
    }
    if (!crlf) {
      _last_code[0] = c;
      _last_code[1] = EOF;
    }
    _last_char = c;
    return c;
  } else {
    /* We need more bytes. First copy unconsumed bytes at the
     * beginning of buffer. */
    cl_index unconsumed_bytes = *buffer_end - *buffer_pos;
    memcpy(buffer, *buffer_pos, unconsumed_bytes);
    cl_index needed_bytes = VECTOR_ENCODING_BUFFER_SIZE;
    if (!seekable && min_needed_bytes < VECTOR_ENCODING_BUFFER_SIZE)
      needed_bytes = min_needed_bytes;
    *buffer_end = buffer + unconsumed_bytes + read_byte8(buffer + unconsumed_bytes, needed_bytes);
    if (*buffer_end == buffer + unconsumed_bytes)
      return EOF;
    *buffer_pos = buffer;
    goto AGAIN;
  }
}

cl_index FileStream_O::read_vector(T_sp data, cl_index start, cl_index end) {
  Vector_sp vec = gc::As<Vector_sp>(data);
  T_sp elementType = vec->element_type();
  if (start >= end)
    return start;
  if (elementType == ext::_sym_byte8 || elementType == ext::_sym_integer8) {
    if (_byte_size == sizeof(uint8_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      return start + read_byte8(aux, end - start);
    }
  } else if (elementType == ext::_sym_byte16 || elementType == ext::_sym_integer16) {
    if (_byte_size == sizeof(uint16_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint16_t);
      bytes = read_byte8(aux, bytes);
      return start + bytes / sizeof(uint16_t);
    }
  } else if (elementType == ext::_sym_byte32 || elementType == ext::_sym_integer32) {
    if (_byte_size == sizeof(uint32_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint32_t);
      bytes = read_byte8(aux, bytes);
      return start + bytes / sizeof(uint32_t);
    }
  } else if (elementType == ext::_sym_byte64 || elementType == ext::_sym_integer64) {
    if (_byte_size == sizeof(uint64_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint64_t);
      bytes = read_byte8(aux, bytes);
      return start + bytes / sizeof(uint64_t);
    }
  } else if (elementType == cl::_sym_fixnum) {
    if (_byte_size == sizeof(Fixnum) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(Fixnum);
      bytes = read_byte8(aux, bytes);
      return start + bytes / sizeof(Fixnum);
    }
  } else if (elementType == cl::_sym_base_char || elementType == cl::_sym_character) {
    unsigned char buffer[VECTOR_ENCODING_BUFFER_SIZE + ENCODING_BUFFER_MAX_SIZE];
    unsigned char* buffer_pos = buffer;
    unsigned char* buffer_end = buffer;
    /* When we can't call lseek/fseek we have to be conservative and
     * read only as many bytes as we actually need. Otherwise, we read
     * more and later reposition the file offset. */
    bool seekable = position().notnilp();

    while (start < end) {
      claspCharacter c = decode_char_from_buffer(buffer, &buffer_pos, &buffer_end, seekable, (end - start) * (_byte_size / 8));
      if (c == EOF)
        break;
      vec->rowMajorAset(start++, clasp_make_character(c));
    }

    if (seekable) {
      /* INV: (buffer_end - buffer_pos) is divisible by \
       * (strm->stream.byte_size / 8) since VECTOR_ENCODING_BUFFER_SIZE \
       * is divisible by all byte sizes for character streams and all \
       * decoders consume bytes in multiples of the byte size. */
      T_sp fp = position();
      if (fp.fixnump()) {
        set_position(contagion_sub(gc::As_unsafe<Number_sp>(fp), make_fixnum((buffer_end - buffer_pos) / (_byte_size / 8))));
      } else {
        SIMPLE_ERROR("clasp_file_position is not a number");
      }
    }

    return start;
  }
  return AnsiStream_O::read_vector(data, start, end);
}

cl_index FileStream_O::write_vector(T_sp data, cl_index start, cl_index end) {
  Vector_sp vec = gc::As<Vector_sp>(data);
  T_sp elementType = vec->element_type();
  if (start >= end)
    return start;
  if (elementType == ext::_sym_byte8 || elementType == ext::_sym_integer8) {
    if (_byte_size == sizeof(uint8_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      return write_byte8(aux, end - start);
    }
  } else if (elementType == ext::_sym_byte16 || elementType == ext::_sym_integer16) {
    if (_byte_size == sizeof(uint16_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint16_t);
      bytes = write_byte8(aux, bytes);
      return start + bytes / sizeof(uint16_t);
    }
  } else if (elementType == ext::_sym_byte32 || elementType == ext::_sym_integer32) {
    if (_byte_size == sizeof(uint32_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint32_t);
      bytes = write_byte8(aux, bytes);
      return start + bytes / sizeof(uint32_t);
    }
  } else if (elementType == ext::_sym_byte64 || elementType == ext::_sym_integer64) {
    if (_byte_size == sizeof(uint64_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(uint64_t);
      bytes = write_byte8(aux, bytes);
      return start + bytes / sizeof(uint64_t);
    }
  } else if (elementType == cl::_sym_fixnum) {
    if (_byte_size == sizeof(Fixnum) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      size_t bytes = (end - start) * sizeof(Fixnum);
      bytes = write_byte8(aux, bytes);
      return start + bytes / sizeof(Fixnum);
    }
  } else if (elementType == _sym_size_t) {
    if (_byte_size == sizeof(size_t) * 8) {
      unsigned char* aux = (unsigned char*)vec->rowMajorAddressOfElement_(start);
      cl_index bytes = (end - start) * sizeof(size_t);
      bytes = write_byte8(aux, bytes);
      return start + bytes / sizeof(size_t);
    }
  } else if (elementType == cl::_sym_base_char) {
    /* 1 extra byte for linefeed in crlf mode */
    unsigned char buffer[VECTOR_ENCODING_BUFFER_SIZE + ENCODING_BUFFER_MAX_SIZE + 1];
    size_t nbytes = 0;
    size_t i;
    for (i = start; i < end; i++) {
      char c = *(char*)(vec->rowMajorAddressOfElement_(i));
      if (c == CLASP_CHAR_CODE_NEWLINE) {
        if ((_flags & CLASP_STREAM_CR) && (_flags & CLASP_STREAM_LF))
          nbytes += encode(buffer + nbytes, CLASP_CHAR_CODE_RETURN);
        else if (_flags & CLASP_STREAM_CR)
          c = CLASP_CHAR_CODE_RETURN;
      }
      nbytes += encode(buffer + nbytes, c);
      update_column(c);
      if (nbytes >= VECTOR_ENCODING_BUFFER_SIZE) {
        write_byte8(buffer, nbytes);
        nbytes = 0;
      }
    }
    write_byte8(buffer, nbytes);
    return end;
  }
#ifdef CLASP_UNICODE
  else if (elementType == cl::_sym_character) {
    /* 1 extra byte for linefeed in crlf mode */
    unsigned char buffer[VECTOR_ENCODING_BUFFER_SIZE + ENCODING_BUFFER_MAX_SIZE + 1];
    cl_index nbytes = 0;
    cl_index i;
    for (i = start; i < end; i++) {
      unsigned char c = *(unsigned char*)vec->rowMajorAddressOfElement_(i);
      if (c == CLASP_CHAR_CODE_NEWLINE) {
        if ((_flags & CLASP_STREAM_CR) && (_flags & CLASP_STREAM_LF))
          nbytes += encode(buffer + nbytes, CLASP_CHAR_CODE_RETURN);
        else if (_flags & CLASP_STREAM_CR)
          c = CLASP_CHAR_CODE_RETURN;
      }
      nbytes += encode(buffer + nbytes, c);
      update_column(c);
      if (nbytes >= VECTOR_ENCODING_BUFFER_SIZE) {
        write_byte8(buffer, nbytes);
        nbytes = 0;
      }
    }
    write_byte8(buffer, nbytes);
    return end;
  }
#endif
  return AnsiStream_O::write_vector(data, start, end);
}

SYMBOL_EXPORT_SC_(KeywordPkg, utf_8);
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_2);
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_2be);
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_2le)
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_4);
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_4be);
SYMBOL_EXPORT_SC_(KeywordPkg, ucs_4le);
SYMBOL_EXPORT_SC_(KeywordPkg, iso_8859_1);
SYMBOL_EXPORT_SC_(KeywordPkg, latin_1);
SYMBOL_EXPORT_SC_(KeywordPkg, us_ascii);
SYMBOL_EXPORT_SC_(ExtPkg, make_encoding);

static int parse_external_format(T_sp tstream, T_sp format, int flags) {
  FileStream_sp stream = gc::As_unsafe<FileStream_sp>(tstream);
  if (format == kw::_sym_default) {
    format = ext::_sym_STARdefault_external_formatSTAR->symbolValue();
  }
  if ((format).consp()) {
    flags = parse_external_format(stream, oCdr(format), flags);
    format = oCar(format);
  }
  if (format == _lisp->_true()) {
#ifdef CLASP_UNICODE
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UTF_8;
#else
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_DEFAULT_FORMAT;
#endif
  }
  if (format == nil<T_O>()) {
    return flags;
  }
  if (format == kw::_sym_cr) {
    return (flags | CLASP_STREAM_CR) & ~CLASP_STREAM_LF;
  }
  if (format == kw::_sym_lf) {
    return (flags | CLASP_STREAM_LF) & ~CLASP_STREAM_CR;
  }
  if (format == kw::_sym_crlf) {
    return flags | (CLASP_STREAM_CR + CLASP_STREAM_LF);
  }
  if (format == kw::_sym_littleEndian) {
    return flags | CLASP_STREAM_LITTLE_ENDIAN;
  }
  if (format == kw::_sym_bigEndian) {
    return flags & ~CLASP_STREAM_LITTLE_ENDIAN;
  }
  if (format == kw::_sym_passThrough) {
#ifdef CLASP_UNICODE
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_LATIN_1;
#else
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_DEFAULT_FORMAT;
#endif
  }
#ifdef CLASP_UNICODE
PARSE_SYMBOLS:
  if (format == kw::_sym_utf_8) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UTF_8;
  }
  if (format == kw::_sym_ucs_2) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_2;
  }
  if (format == kw::_sym_ucs_2be) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_2BE;
  }
  if (format == kw::_sym_ucs_2le) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_2LE;
  }
  if (format == kw::_sym_ucs_4) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_4;
  }
  if (format == kw::_sym_ucs_4be) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_4BE;
  }
  if (format == kw::_sym_ucs_4le) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_UCS_4LE;
  }
  if (format == kw::_sym_iso_8859_1) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_ISO_8859_1;
  }
  if (format == kw::_sym_latin_1) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_LATIN_1;
  }
  if (format == kw::_sym_us_ascii) {
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_US_ASCII;
  }
  if (gc::IsA<HashTable_sp>(format)) {
    stream->_format_table = format;
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_USER_FORMAT;
  }
  if (gc::IsA<Symbol_sp>(format)) {
    ASSERT(format.notnilp());
    format = eval::funcall(ext::_sym_make_encoding, format);
    if (gc::IsA<Symbol_sp>(format))
      goto PARSE_SYMBOLS;
    stream->_format_table = format;
    return (flags & ~CLASP_STREAM_FORMAT) | CLASP_STREAM_USER_FORMAT;
  }
#endif
  FEerror("Unknown or unsupported external format: ~A", 1, format.raw_());
  return CLASP_STREAM_DEFAULT_FORMAT;
}

T_sp FileStream_O::set_element_type(T_sp type) {
  // Need to add logic here
  return _element_type = type;
}

T_sp FileStream_O::set_external_format(T_sp format) {
  T_sp t;
  if (_byte_size < 0) {
    _byte_size = -_byte_size;
    _flags |= CLASP_STREAM_SIGNED_BYTES;
    t = cl::_sym_SignedByte;
  } else {
    _flags &= ~CLASP_STREAM_SIGNED_BYTES;
    t = cl::_sym_UnsignedByte;
  }
  _flags = parse_external_format(asSmartPtr(), format, _flags);
  switch (_flags & CLASP_STREAM_FORMAT) {
  case CLASP_STREAM_BINARY:
    // e.g. (T size) is not a valid type, use (UnsignedByte size)
    // This is better than (T Size), but not necesarily the right type
    // Probably the value of the vriable t was meant, use it now!
    _element_type = Cons_O::createList(t, make_fixnum(_byte_size));
    _format = t;
    break;
#ifdef CLASP_UNICODE
  /*case ECL_ISO_8859_1:*/
  case CLASP_STREAM_LATIN_1:
    _element_type = cl::_sym_base_char;
    _byte_size = 8;
    _format = kw::_sym_latin_1;
    break;
  case CLASP_STREAM_UTF_8:
    _element_type = cl::_sym_character;
    _byte_size = 8;
    _format = kw::_sym_utf_8;
    break;
  case CLASP_STREAM_UCS_2:
    _element_type = cl::_sym_character;
    _byte_size = 8 * 2;
    _format = kw::_sym_ucs_2;
    break;
  case CLASP_STREAM_UCS_2BE:
    _element_type = cl::_sym_character;
    _byte_size = 8 * 2;
    if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
      _format = kw::_sym_ucs_2le;
    } else {
      _format = kw::_sym_ucs_2be;
    }
    break;
  case CLASP_STREAM_UCS_4:
    _element_type = cl::_sym_character;
    _byte_size = 8 * 4;
    _format = kw::_sym_ucs_4be;
    break;
  case CLASP_STREAM_UCS_4BE:
    _element_type = cl::_sym_character;
    _byte_size = 8 * 4;
    if (_flags & CLASP_STREAM_LITTLE_ENDIAN) {
      _format = kw::_sym_ucs_4le;
    } else {
      _format = kw::_sym_ucs_4be;
    }
    break;
  case CLASP_STREAM_USER_FORMAT:
    _element_type = cl::_sym_character;
    _byte_size = 8;
    _format = _format_table;
    if (_format_table.consp())
      _flags |= CLASP_STREAM_USER_MULTISTATE_FORMAT;
    break;
  case CLASP_STREAM_US_ASCII:
    _element_type = cl::_sym_base_char;
    _byte_size = 8;
    _format = kw::_sym_us_ascii;
    break;
#else
  case CLASP_STREAM_DEFAULT_FORMAT:
    _element_type = cl::_sym_base_char;
    _byte_size = 8;
    _format = kw::_sym_passThrough;
    break;
#endif
  default:
    FEerror("Invalid or unsupported external format ~A with code ~D", 2, format.raw_(), make_fixnum(_flags).raw_());
  }
  t = kw::_sym_lf;
  if (_flags & CLASP_STREAM_CR) {
    if (_flags & CLASP_STREAM_LF) {
      t = kw::_sym_crlf;
    } else {
      t = kw::_sym_cr;
    }
  }
  _format = Cons_O::createList(_format, t);
  _byte_size = (_byte_size + 7) & (~(gctools::Fixnum)7);

  return format;
}

/**********************************************************************
 * C STREAMS
 */

void IOStreamStream_O::fixupInternalsForSnapshotSaveLoad(snapshotSaveLoad::Fixup* fixup) {
  if (snapshotSaveLoad::operation(fixup) == snapshotSaveLoad::LoadOp) {
    std::string name = gc::As<String_sp>(_filename)->get_std_string();
    T_sp stream = this->asSmartPtr();
    if (name == "*STDIN*") {
      _file = stdin;
    } else if (name == "*STDOUT*") {
      _file = stdout;
    } else if (name == "*STDERR*") {
      _file = stderr;
    }
  }
}

cl_index IOStreamStream_O::read_byte8(unsigned char* c, cl_index n) {
  check_input();

  if (_mode == stream_mode_io) {
    if (_last_op < 0) {
      force_output();
    }
    _last_op = +1;
  }

  unlikely_if(_byte_stack.notnilp()) return consume_byte_stack(c, n);

  gctools::Fixnum out = 0;
  clasp_disable_interrupts();
  do {
    out = fread(c, sizeof(char), n, _file);
  } while (out < n && ferror(_file) && restartable_io_error("fread"));
  clasp_enable_interrupts();

  return out;
}

cl_index IOStreamStream_O::write_byte8(unsigned char* c, cl_index n) {
  if (input_p()) {
    /* When using the same stream for input and output operations, we have to
     * use some file position operation before reading again. Besides this, if
     * there were unread octets, we have to move to the position at the
     * begining of them.
     */
    if (_byte_stack.notnilp()) { //  != nil<T_O>()) {
      T_sp aux = stream_position(asSmartPtr());
      if (!aux.nilp())
        stream_set_position(asSmartPtr(), aux);
    } else if (_last_op > 0) {
      clasp_fseeko(_file, 0, SEEK_CUR);
    }
    _last_op = -1;
  }

  cl_index out;
  clasp_disable_interrupts();
  do {
    out = fwrite(c, sizeof(char), n, _file);
  } while (out < n && restartable_io_error("fwrite"));
  clasp_enable_interrupts();
  return out;
}

ListenResult IOStreamStream_O::listen() {
  check_input();
  if (_byte_stack.notnilp())
    return listen_result_available;
  return _file_listen();
}

void IOStreamStream_O::clear_input() {
  check_input();
#if defined(CLASP_MS_WINDOWS_HOST)
  int f = fileno(_file);
  if (isatty(f)) {
    /* Flushes Win32 console */
    unlikely_if(!FlushConsoleInputBuffer((HANDLE)_get_osfhandle(f))) FEwin32_error("FlushConsoleInputBuffer() failed", 0);
    /* Do not stop here: the FILE structure needs also to be flushed */
  }
#endif
  while (_file_listen() == listen_result_available) {
    clasp_disable_interrupts();
    getc(_file);
    clasp_enable_interrupts();
  }
}

void IOStreamStream_O::clear_output() { check_output(); }

void IOStreamStream_O::force_output() {
  check_output();
  clasp_disable_interrupts();
  while ((fflush(_file) == EOF) && restartable_io_error("fflush"))
    (void)0;
  clasp_enable_interrupts();
}

void IOStreamStream_O::finish_output() { force_output(); }

bool IOStreamStream_O::interactive_p() const { return isatty(fileno(_file)); }

T_sp IOStreamStream_O::length() {
  T_sp output = clasp_file_len(fileno(_file)); // NIL or Integer_sp
  if (_byte_size != 8 && output.notnilp()) {
    //            const cl_env_ptr the_env = clasp_process_env();
    T_mv output_mv = clasp_floor2(gc::As_unsafe<Integer_sp>(output), make_fixnum(_byte_size / 8));
    // and now lets use the calculated value
    output = output_mv;
    MultipleValues& mvn = core::lisp_multipleValues();
    Fixnum_sp ofn1 = gc::As<Fixnum_sp>(mvn.valueGet(1, output_mv.number_of_values()));
    Fixnum fn = unbox_fixnum(ofn1);
    unlikely_if(fn != 0) { FEerror("File length is not on byte boundary", 0); }
  }
  return output;
}

T_sp IOStreamStream_O::position() {
  T_sp output;
  clasp_off_t offset;

  clasp_disable_interrupts();
  offset = clasp_ftello(_file);
  clasp_enable_interrupts();
  if (offset < 0) {
    return make_fixnum(0);
    // io_error(strm);
  }
  if (sizeof(clasp_off_t) == sizeof(long)) {
    output = Integer_O::create((gctools::Fixnum)offset);
  } else {
    output = clasp_off_t_to_integer(offset);
  }
  {
    /* If there are unread octets, we return the position at which
     * these bytes begin! */
    T_sp l = _byte_stack;
    while ((l).consp()) {
      output = clasp_one_minus(gc::As<Integer_sp>(output));
      l = oCdr(l);
    }
  }
  if (_byte_size != 8) {
    output = clasp_floor2(gc::As<Integer_sp>(output), make_fixnum(_byte_size / 8));
  }
  return output;
}

T_sp IOStreamStream_O::set_position(T_sp pos) {
  clasp_off_t disp;
  int mode;
  if (pos.nilp()) {
    disp = 0;
    mode = SEEK_END;
  } else {
    if (_byte_size != 8) {
      pos = clasp_times(gc::As<Integer_sp>(pos), make_fixnum(_byte_size / 8));
    }
    disp = clasp_integer_to_off_t(pos);
    mode = SEEK_SET;
  }
  clasp_disable_interrupts();
  mode = clasp_fseeko(_file, disp, mode);
  clasp_enable_interrupts();
  return mode ? nil<T_O>() : _lisp->_true();
}

int IOStreamStream_O::input_handle() { return (_mode == stream_mode_input || _mode == stream_mode_io) ? fileno(_file) : -1; }

int IOStreamStream_O::output_handle() { return (_mode == stream_mode_output || _mode == stream_mode_io) ? fileno(_file) : -1; }

T_sp IOStreamStream_O::close(T_sp abort) {
  if (_open) {
    int failed;
    unlikely_if(_file == stdout) FEerror("Cannot close the standard output", 0);
    unlikely_if(_file == stdin) FEerror("Cannot close the standard input", 0);
    unlikely_if(_file == NULL) wrong_file_handler(asSmartPtr());
    if (output_p())
      force_output();
    failed = safe_fclose(_file);
    unlikely_if(failed) cannot_close(asSmartPtr());
    gctools::clasp_dealloc(_buffer);
    _buffer = NULL;
    _file = NULL;
    close_cleanup(abort);
    _open = false;
  }
  return _lisp->_true();
}

T_sp IOStreamStream_O::make(T_sp fname, FILE* f, StreamMode smm, gctools::Fixnum byte_size, int flags, T_sp external_format,
                            T_sp tempName, bool created) {
  IOStreamStream_sp stream = IOStreamStream_O::create();
  stream->_temp_filename = tempName;
  stream->_created = created;
  stream->_mode = smm;
  stream->_open = true;
  stream->_byte_size = byte_size;
  stream->_flags = flags;
  stream->set_external_format(external_format);
  stream->_filename = fname;
  stream->_file = f;
  return stream;
}

/**********************************************************************
 * WINSOCK STREAMS
 */

#ifdef ECL_WSOCK

cl_index WinsockStream_O::read_byte8(unsigned char* c, cl_index n) {
  cl_index len = 0;

  unlikely_if(_byte_stack.notnilp()) { return consume_byte_stack(c, n); }
  if (n > 0) {
    unlikely_if(INVALID_SOCKET == _socket) wrong_file_handler(asSmartPtr());
    else {
      clasp_disable_interrupts();
      len = recv(_socket, c, n, 0);
      unlikely_if(len == SOCKET_ERROR) wsock_error("Cannot read bytes from Windows "
                                                   "socket ~S.~%~A",
                                                   strm);
      clasp_enable_interrupts();
    }
  }
  return (len > 0) ? len : EOF;
}

cl_index WinsockStream_O::write_byte8(unsigned char* c, cl_index n) {
  cl_index out = 0;
  unsigned char* endp;
  unsigned char* p;
  unlikely_if(INVALID_SOCKET == _socket) wrong_file_handler(asSmartPtr());
  else {
    clasp_disable_interrupts();
    do {
      cl_index res = send(_socket, c + out, n, 0);
      unlikely_if(res == SOCKET_ERROR) {
        wsock_error("Cannot write bytes to Windows"
                    " socket ~S.~%~A",
                    strm);
        break; /* stop writing */
      }
      else {
        out += res;
        n -= res;
      }
    } while (n > 0);
    clasp_enable_interrupts();
  }
  return out;
}

ListenResult WinsockStream_O::listen() {
  unlikely_if(_byte_stack.notnilp()) return listen_result_available;

  unlikely_if(INVALID_SOCKET == _socket) wrong_file_handler(asSmartPtr());
  {
    struct timeval tv = {0, 0};
    fd_set fds;
    cl_index result;

    FD_ZERO(&fds);
    FD_SET(_socket, &fds);
    clasp_disable_interrupts();
    result = select(0, &fds, NULL, NULL, &tv);
    unlikely_if(result == SOCKET_ERROR) wsock_error("Cannot listen on Windows "
                                                    "socket ~S.~%~A",
                                                    strm);
    clasp_enable_interrupts();
    return (result > 0 ? listen_result_available : listen_result_no_char);
  }
}

void WinsockStream_O::clear_input() {
  while (listen() == listen_result_available) {
    read_char();
  }
}

T_sp WinsockStream_O::close(T_sp abort) {
  if (_open) {
    _open = false;
    int failed;
    clasp_disable_interrupts();
    failed = closesocket(_socket);
    clasp_enable_interrupts();
    unlikely_if(failed < 0) cannot_close(asSmartPtr());
    _socket = INVALID_SOCKET;
  }
  return _lisp->_true();
}

T_sp WinsockStream_O::make(T_sp fname, SOCKET socket, StreamMode smm, gctools::Fixnum byte_size, int flags, T_sp external_format) {
  WinsockStream_sp stream = WinsockStream_O::create();
  stream->_socket = socket;
  stream->_mode = smm;
  stream->_open = true;
  stream->_byte_size = byte_size;
  stream->_flags = flags;
  stream->set_external_format(external_format);
  stream->_filename = fname;
  return stream;
}

#endif

/**********************************************************************
 * WINCONSOLE STREAM
 */

#if defined(CLASP_MS_WINDOWS_HOST)

bool ConsoleStream_O::interactive_p() const {
  DWORD mode;
  return !!GetConsoleMode(_handle, &mode);
}

cl_index ConsoleStream_O::read_byte8(unsigned char* c, cl_index n) {
  unlikely_if(_byte_stack.notnilp()) { return consume_byte_stack(c, n); }
  else {
    cl_index len = 0;
    cl_env_ptr the_env = clasp_process_env();
    DWORD nchars;
    unsigned char aux[4];
    for (len = 0; len < n;) {
      int i, ok;
      clasp_disable_interrupts_env(the_env);
      ok = ReadConsole(_handle, &aux, 1, &nchars, NULL);
      clasp_enable_interrupts_env(the_env);
      unlikely_if(!ok) { FEwin32_error("Cannot read from console", 0); }
      for (i = 0; i < nchars; i++) {
        if (len < n) {
          c[len++] = aux[i];
        } else {
          _byte_stack = clasp_nconc(_byte_stack, clasp_list1(make_fixnum(aux[i])));
        }
      }
    }
    return (len > 0) ? len : EOF;
  }
}

cl_index ConsoleStream_O::write_byte8(unsigned char* c, cl_index n) {
  DWORD nchars;
  unlikely_if(!WriteConsole(_handle, c, n, &nchars, NULL)) { FEwin32_error("Cannot write to console.", 0); }
  return nchars;
}

int ConsoleStream_O::listen(T_sp) {
  INPUT_RECORD aux;
  DWORD nevents;
  do {
    unlikely_if(!PeekConsoleInput(_handle, &aux, 1, &nevents)) FEwin32_error("Cannot read from console.", 0);
    if (nevents == 0)
      return 0;
    if (aux.EventType == KEY_EVENT)
      return 1;
    unlikely_if(!ReadConsoleInput(_handle), &aux, 1, &nevents) FEwin32_error("Cannot read from console.", 0);
  } while (1);
}

void ConsoleStream_O::clear_input() { FlushConsoleInputBuffer(_handle); }

void ConsoleStream_O::force_output(T_sp strm) {
  DWORD nchars;
  WriteConsole(_handle, 0, 0, &nchars, NULL);
}

#define CONTROL_Z 26

T_sp ConsoleStream_O::make(T_sp fname, HANDLE handle, StreamMode smm, gctools::Fixnum byte_size, int flags, T_sp external_format) {
  ConsoleStream_sp stream = ConsoleStream_O::create();
  stream->_handle = handle;
  stream->_mode = smm;
  stream->_open = true;
  stream->_byte_size = byte_size;
  stream->_flags = flags;
  stream->set_external_format(external_format);
  stream->_filename = fname;
  if (stream->interactive_p())
    stream->_eof_char = CONTROL_Z;
  return stream;
}

#endif

CL_LAMBDA(stream mode);
CL_DECLARE();
CL_DOCSTRING(R"dx(set-buffering-mode)dx");
DOCGROUP(clasp);
CL_LISPIFY_NAME("set_buffering_mode")
CL_DEFMETHOD
void IOStreamStream_O::set_buffering_mode(T_sp mode) {
  int bm;

  if (mode == kw::_sym_none || mode.nilp())
    bm = _IONBF;
  else if (mode == kw::_sym_line || mode == kw::_sym_line_buffered)
    bm = _IOLBF;
  else if (mode == kw::_sym_full || mode == kw::_sym_fully_buffered)
    bm = _IOFBF;
  else
    FEerror("Not a valid buffering mode: ~A", 1, mode.raw_());

  if (bm != _IONBF) {
    _buffer = gctools::clasp_alloc_atomic(BUFSIZ);
    setvbuf(_file, _buffer, bm, BUFSIZ);
  } else
    setvbuf(_file, NULL, _IONBF, 0);
}

T_sp IOStreamStream_O::make(T_sp fname, int fd, StreamMode smm, gctools::Fixnum byte_size, int flags, T_sp external_format,
                            T_sp tempName, bool created) {
  const char* mode; /* file open mode */
  FILE* fp;         /* file pointer */
  switch (smm) {
  case stream_mode_input:
    mode = OPEN_R;
    break;
  case stream_mode_output:
    mode = OPEN_W;
    break;
  case stream_mode_io:
    mode = OPEN_RW;
    break;
  default:
    mode = OPEN_R; // dummy
    FEerror("make_stream: wrong mode in IOStreamStream_O::make smm = ~d", 1, clasp_make_fixnum(smm).raw_());
  }
  fp = safe_fdopen(fd, mode);
  if (fp == NULL) {
    struct stat info;
    int fstat_error = fstat(fd, &info);
    if (fstat_error != 0) {
      SIMPLE_ERROR("Unable to create stream for file descriptor and while running fstat another error occurred -> fd: {} name: {} "
                   "mode: %s error: %s | fstat_error = %d  info.st_mode = %08x%s",
                   fd, gc::As<String_sp>(fname)->get_std_string().c_str(), mode, strerror(errno), fstat_error, info.st_mode,
                   string_mode(info.st_mode));
    }
    SIMPLE_ERROR(
        "Unable to create stream for file descriptor %ld name: %s mode: %s error: %s | fstat_error = %d  info.st_mode = %08x%s", fd,
        gc::As<String_sp>(fname)->get_std_string().c_str(), mode, strerror(errno), fstat_error, info.st_mode,
        string_mode(info.st_mode));
  }
  return IOStreamStream_O::make(fname, fp, smm, byte_size, flags, external_format, tempName, created);
}

SYMBOL_EXPORT_SC_(KeywordPkg, input_output);

CL_LAMBDA(fd direction &key buffering element-type (external-format :default) (name "FD-STREAM"));
CL_DEFUN T_sp ext__make_stream_from_fd(int fd, T_sp direction, T_sp buffering, T_sp element_type, T_sp external_format,
                                       String_sp name) {
  StreamMode smm_mode = stream_mode_output;
  if (direction == kw::_sym_input) {
    smm_mode = stream_mode_input;
  } else if (direction == kw::_sym_output) {
    smm_mode = stream_mode_output;
  } else if (direction == kw::_sym_io || direction == kw::_sym_input_output) {
    smm_mode = stream_mode_io;
  } else {
    SIMPLE_ERROR("Unknown smm_mode");
  }
  if (cl__integerp(element_type)) {
    external_format = nil<T_O>();
  }
  gctools::Fixnum byte_size;
  byte_size = clasp_normalize_stream_element_type(element_type);
  IOStreamStream_sp stream = IOStreamStream_O::make(name, fd, smm_mode, byte_size, CLASP_STREAM_BINARY, external_format);
  if (buffering.notnilp()) {
    stream->set_buffering_mode(byte_size ? kw::_sym_full : kw::_sym_line);
  }
  return stream;
}

CL_LAMBDA(s);
CL_DECLARE();
CL_DOCSTRING(R"dx(Returns the file descriptor for a stream)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp ext__file_stream_file_descriptor(T_sp s) {
  return clasp_make_fixnum(stream_output_p(s) ? stream_output_handle(s) : stream_input_handle(s));
}

// Temporary shim until we can update SLIME.
DOCGROUP(clasp);
CL_DEFUN T_sp core__file_stream_fd(T_sp s) { return ext__file_stream_file_descriptor(s); }

/**********************************************************************
 * MEDIUM LEVEL INTERFACE
 */

claspCharacter stream_read_char_noeof(T_sp strm) {
  claspCharacter c = stream_read_char(strm);
  if (c == EOF)
    ERROR_END_OF_FILE(strm);
  return c;
}

/*******************************tl***************************************
 * SEQUENCES I/O
 */

void writestr_stream(const char* s, T_sp strm) {
  while (*s != '\0')
    stream_write_char(strm, *s++);
}

CL_LAMBDA(oject stream);
CL_DECLARE();
CL_DOCSTRING("Write the address of an object to the stream designator.");
DOCGROUP(clasp);
CL_DEFUN void core__write_addr(T_sp x, T_sp strm) {
  stringstream ss;
  ss << (void*)x.raw_();
  writestr_stream(ss.str().c_str(), coerce::outputStreamDesignator(strm));
}

CL_LAMBDA(stream string);
CL_DECLARE();
CL_DOCSTRING(R"dx(file-string-length)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__file_string_length(T_sp stream, T_sp tstring) { return stream_string_length(stream, tstring); }

CL_LAMBDA(seq stream start end);
CL_DECLARE();
CL_DOCSTRING(R"dx(do_write_sequence)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__do_write_sequence(T_sp seq, T_sp stream, T_sp s, T_sp e) {
  gctools::Fixnum start, limit, end(0);

  /* Since we have called clasp_length(), we know that SEQ is a valid
           sequence. Therefore, we only need to check the type of the
           object, and seq == nil<T_O>() i.f.f. t = t_symbol */
  limit = cl__length(seq);
  if (!core__fixnump(s)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_start, s, Integer_O::makeIntegerType(0, limit - 1));
  }
  start = (s).unsafe_fixnum();
  if ((start < 0) || (start > limit)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_start, s, Integer_O::makeIntegerType(0, limit - 1));
  }
  if (e.nilp()) {
    end = limit;
  } else if (!e.fixnump()) { //! core__fixnump(e)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_end, e, Integer_O::makeIntegerType(0, limit));
  } else
    end = (e).unsafe_fixnum();
  if ((end < 0) || (end > limit)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_end, e, Integer_O::makeIntegerType(0, limit));
  }
  if (start < end) {
    if (cl__listp(seq)) {
      T_sp elt_type = cl__stream_element_type(stream);
      bool ischar = (elt_type == cl::_sym_base_char) || (elt_type == cl::_sym_character);
      T_sp s = cl__nthcdr(clasp_make_integer(start), seq);
      T_sp orig = s;
      for (; s.notnilp(); s = oCdr(s)) {
        if (!cl__listp(s)) {
          TYPE_ERROR_PROPER_LIST(orig);
        }
        if (start < end) {
          T_sp elt = oCar(s);
          if (ischar)
            stream_write_char(stream, clasp_as_claspCharacter(gc::As<Character_sp>(elt)));
          else
            stream_write_byte(stream, elt);
          start++;
        } else {
          return seq;
        }
      };
    } else {
      stream_write_vector(stream, seq, start, end);
    }
  }
  return seq;
}

T_sp si_do_read_sequence(T_sp seq, T_sp stream, T_sp s, T_sp e) {
  gctools::Fixnum start, limit, end(0);
  /* Since we have called clasp_length(), we know that SEQ is a valid
           sequence. Therefore, we only need to check the type of the
           object, and seq == nil<T_O>() i.f.f. t = t_symbol */
  limit = cl__length(seq);
  if (!core__fixnump(s)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_read_sequence, kw::_sym_start, s, Integer_O::makeIntegerType(0, limit - 1));
  }
  start = (s).unsafe_fixnum();
  if ((start < 0) || (start > limit)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_read_sequence, kw::_sym_start, s, Integer_O::makeIntegerType(0, limit - 1));
  }
  if (e.nilp()) {
    end = limit;
  } else if (!e.fixnump()) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_read_sequence, kw::_sym_end, e, Integer_O::makeIntegerType(0, limit));
  } else {
    end = (e).unsafe_fixnum();
  }
  if ((end < 0) || (end > limit)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_read_sequence, kw::_sym_end, e, Integer_O::makeIntegerType(0, limit));
  }
  if (end < start) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_read_sequence, kw::_sym_end, e, Integer_O::makeIntegerType(start, limit));
  }
  if (start < end) {
    if (cl__listp(seq)) {
      T_sp elt_type = cl__stream_element_type(stream);
      bool ischar = (elt_type == cl::_sym_base_char) || (elt_type == cl::_sym_character);
      seq = cl__nthcdr(clasp_make_integer(start), seq);
      for (; seq.notnilp(); seq = oCdr(seq)) {
        if (start >= end) {
          return make_fixnum(start);
        } else {
          T_sp c;
          if (ischar) {
            int i = stream_read_char(stream);
            if (i < 0) {
              return make_fixnum(start);
            }
            c = clasp_make_character(i);
          } else {
            c = stream_read_byte(stream);
            if (c.nilp()) {
              return make_fixnum(start);
            }
          }
          gc::As<Cons_sp>(seq)->rplaca(c);
          start++;
        }
      };
    } else {
      start = stream_read_vector(stream, seq, start, end);
    }
  }
  return make_fixnum(start);
}

CL_LAMBDA(sequence stream &key (start 0) end);
CL_DECLARE();
CL_DOCSTRING(R"dx(readSequence)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__read_sequence(T_sp sequence, T_sp stream, T_sp start, T_sp oend) {
  stream = coerce::inputStreamDesignator(stream);
  return stream.isA<AnsiStream_O>() ? si_do_read_sequence(sequence, stream, start, oend)
                                    : eval::funcall(gray::_sym_stream_read_sequence, stream, sequence, start, oend);
}

CL_LAMBDA(sequence stream start end);
CL_DECLARE();
CL_DOCSTRING(R"dx(readSequence)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__do_read_sequence(T_sp sequence, T_sp stream, T_sp start, T_sp oend) {
  stream = coerce::inputStreamDesignator(stream);
  return si_do_read_sequence(sequence, stream, start, oend);
}
/**********************************************************************
 * LISP LEVEL INTERFACE
 */

T_sp si_file_column(T_sp strm) { return make_fixnum(stream_column(strm)); }

CL_LAMBDA(strm);
CL_DECLARE();
CL_DOCSTRING(R"dx(file_length)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__file_length(T_sp strm) { return stream_length(strm); }

CL_LAMBDA(file-stream &optional position);
CL_DECLARE();
CL_DOCSTRING(R"dx(filePosition)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__file_position(T_sp stream, T_sp position) {
  T_sp output;
  if (position.nilp()) {
    output = stream_position(stream);
  } else {
    if (position == kw::_sym_start) {
      position = make_fixnum(0);
    } else if (position == kw::_sym_end) {
      position = nil<T_O>();
    }
    output = stream_set_position(stream, position);
  }
  return output;
}

CL_LAMBDA(strm);
DOCGROUP(clasp);
CL_DEFUN T_sp core__input_stream_pSTAR(T_sp strm) {
  ASSERT(strm);
  return (stream_input_p(strm) ? _lisp->_true() : nil<T_O>());
}

CL_LAMBDA(strm);
DOCGROUP(clasp);
CL_DEFUN T_sp cl__input_stream_p(T_sp strm) { return core__input_stream_pSTAR(strm); }

CL_LAMBDA(arg);
DOCGROUP(clasp);
CL_DEFUN T_sp core__output_stream_pSTAR(T_sp strm) {
  ASSERT(strm);
  return stream_output_p(strm) ? _lisp->_true() : nil<T_O>();
}

CL_LAMBDA(arg);
DOCGROUP(clasp);
CL_DEFUN T_sp cl__output_stream_p(T_sp strm) { return core__output_stream_pSTAR(strm); }

CL_LAMBDA(arg);
CL_DECLARE();
CL_DOCSTRING(R"dx(interactive_stream_p)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__interactive_stream_p(T_sp strm) {
  ASSERT(strm);
  return stream_interactive_p(strm) ? _lisp->_true() : nil<T_O>();
}

DOCGROUP(clasp);
CL_DEFUN T_sp core__open_stream_pSTAR(T_sp strm) {
  /* ANSI and Cltl2 specify that open-stream-p should work
           on closed streams, and that a stream is only closed
           when #'close has been applied on it */
  return stream_open_p(strm) ? _lisp->_true() : nil<T_O>();
}

DOCGROUP(clasp);
CL_DEFUN T_sp cl__open_stream_p(T_sp strm) {
  /* ANSI and Cltl2 specify that open-stream-p should work
           on closed streams, and that a stream is only closed
           when #'close has been applied on it */
  return core__open_stream_pSTAR(strm);
}

DOCGROUP(clasp);
CL_DEFUN T_sp core__stream_element_typeSTAR(T_sp strm) { return stream_element_type(strm); }

DOCGROUP(clasp);
CL_DEFUN T_sp cl__stream_element_type(T_sp strm) { return core__stream_element_typeSTAR(strm); }

DOCGROUP(clasp);
CL_DEFUN T_sp cl__stream_external_format(T_sp strm) { return stream_external_format(strm); }

DOCGROUP(clasp);
CL_DEFUN T_sp core__set_stream_external_format(T_sp stream, T_sp format) { return stream_set_external_format(stream, format); }

CL_LAMBDA(arg);
CL_DECLARE();
CL_DOCSTRING(R"dx(streamp)dx");
DOCGROUP(clasp);
CL_DEFUN bool cl__streamp(T_sp stream) { return stream_p(stream); }

/**********************************************************************
 * FILE OPENING AND CLOSING
 */

// Max number of bits in the element type for a byte stream. Arbitrary.
#define BYTE_STREAM_MAX_BITS 1024

gctools::Fixnum clasp_normalize_stream_element_type(T_sp element_type) {
  gctools::Fixnum sign = 0;
  cl_index size;
  if (element_type == cl::_sym_SignedByte || element_type == ext::_sym_integer8) {
    return -8;
  } else if (element_type == cl::_sym_UnsignedByte || element_type == ext::_sym_byte8) {
    return 8;
  } else if (element_type == kw::_sym_default) {
    return 0;
  } else if (element_type == cl::_sym_base_char || element_type == cl::_sym_character) {
    return 0;
  } else if (T_sp(eval::funcall(cl::_sym_subtypep, element_type, cl::_sym_character)).notnilp()) {
    return 0;
  } else if (T_sp(eval::funcall(cl::_sym_subtypep, element_type, cl::_sym_UnsignedByte)).notnilp()) {
    sign = +1;
  } else if (T_sp(eval::funcall(cl::_sym_subtypep, element_type, cl::_sym_SignedByte)).notnilp()) {
    sign = -1;
  } else {
    FEerror("Not a valid stream element type: ~A", 1, element_type.raw_());
  }
  if ((element_type).consp()) {
    if (oCar(element_type) == cl::_sym_UnsignedByte) {
      gc::Fixnum writ = clasp_to_integral<gctools::Fixnum>(oCadr(element_type));
      // Upgrade
      if (writ < 0)
        goto err;
      for (size = 8; size <= BYTE_STREAM_MAX_BITS; size += 8)
        if (writ <= size)
          return size;
      // Too big
      goto err;
    } else if (oCar(element_type) == cl::_sym_SignedByte) {
      gc::Fixnum writ = clasp_to_integral<gctools::Fixnum>(oCadr(element_type));
      if (writ < 0)
        goto err;
      for (size = 8; size <= BYTE_STREAM_MAX_BITS; size += 8)
        if (writ <= size)
          return -size;
      goto err;
    }
  }
  for (size = 8; size <= BYTE_STREAM_MAX_BITS; size += 8) {
    T_sp type;
    type = Cons_O::createList(sign > 0 ? cl::_sym_UnsignedByte : cl::_sym_SignedByte, make_fixnum(size));
    if (T_sp(eval::funcall(cl::_sym_subtypep, element_type, type)).notnilp()) {
      return size * sign;
    }
  }
err:
  FEerror("Not a valid stream element type: ~A", 1, element_type.raw_());
}

static void FEinvalid_option(T_sp option, T_sp value) { FEerror("Invalid value op option ~A: ~A", 2, option.raw_(), value.raw_()); }

T_sp clasp_open_stream(T_sp fn, StreamMode smm, T_sp if_exists, T_sp if_does_not_exist, gctools::Fixnum byte_size, int flags,
                       T_sp external_format) {
  AnsiStream_sp output;
  int f;
#if defined(CLASP_MS_WINDOWS_HOST)
  clasp_mode_t mode = _S_IREAD | _S_IWRITE;
#else
  clasp_mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
#endif
  if (fn.nilp())
    SIMPLE_ERROR("In {} the filename is NIL", __FUNCTION__);
  String_sp filename = core__coerce_to_filename(fn);
  string fname = filename->get_std_string();
  T_sp temp_name = nil<T_O>();
  bool appending = false, created = false;
  ASSERT(filename);
  bool exists = core__file_kind(filename, true).notnilp();
  if (smm == stream_mode_input || smm == stream_mode_probe) {
    if (!exists) {
      if (if_does_not_exist == kw::_sym_error) {
        FEdoes_not_exist(fn);
      } else if (if_does_not_exist == kw::_sym_create) {
        f = safe_open(fname.c_str(), O_WRONLY | O_CREAT, mode);
        unlikely_if(f < 0) FEcannot_open(fn);
        safe_close(f);
      } else if (if_does_not_exist.nilp()) {
        return nil<T_O>();
      } else {
        FEinvalid_option(kw::_sym_if_does_not_exist, if_does_not_exist);
      }
    }
    f = safe_open(fname.c_str(), O_RDONLY, mode);
    unlikely_if(f < 0) FEcannot_open(fn);
  } else if (smm == stream_mode_output || smm == stream_mode_io) {
    int base = (smm == stream_mode_output) ? O_WRONLY : O_RDWR;
    if (if_exists == kw::_sym_new_version && if_does_not_exist == kw::_sym_create) {
      exists = false;
      if_does_not_exist = kw::_sym_create;
    }
    if (exists) {
      if (if_exists == kw::_sym_error) {
        FEexists(fn);
      } else if (if_exists == kw::_sym_rename) {
        f = clasp_backup_open(fname.c_str(), base | O_CREAT, mode);
        unlikely_if(f < 0) FEcannot_open(fn);
      } else if (if_exists == kw::_sym_rename_and_delete || if_exists == kw::_sym_new_version || if_exists == kw::_sym_supersede) {
        temp_name = core__mkstemp(filename);
        f = safe_open(core__coerce_to_filename(temp_name)->get_std_string().c_str(), base | O_CREAT, mode);
        unlikely_if(f < 0) FEcannot_open(fn);
      } else if (if_exists == kw::_sym_overwrite || if_exists == kw::_sym_append) {
        f = safe_open(fname.c_str(), base, mode);
        unlikely_if(f < 0) FEcannot_open(fn);
        appending = (if_exists == kw::_sym_append);
      } else if (if_exists.nilp()) {
        return nil<T_O>();
      } else {
        FEinvalid_option(kw::_sym_if_exists, if_exists);
      }
    } else {
      if (if_does_not_exist == kw::_sym_error) {
        FEdoes_not_exist(fn);
      } else if (if_does_not_exist == kw::_sym_create) {
        f = safe_open(fname.c_str(), base | O_CREAT | O_TRUNC, mode);
        created = true;
        unlikely_if(f < 0) FEcannot_open(fn);
      } else if (if_does_not_exist.nilp()) {
        return nil<T_O>();
      } else {
        FEinvalid_option(kw::_sym_if_does_not_exist, if_does_not_exist);
      }
    }
  } else {
    FEerror("Illegal stream mode ~S", 1, make_fixnum(smm).raw_());
  }
  if (flags & CLASP_STREAM_C_STREAM) {
    FILE* fp = NULL;
    switch (smm) {
    case stream_mode_probe:
    case stream_mode_input:
      fp = safe_fdopen(f, OPEN_R);
      break;
    case stream_mode_output:
      fp = safe_fdopen(f, OPEN_W);
      break;
    case stream_mode_io:
      fp = safe_fdopen(f, OPEN_RW);
      break;
    default:; /* never reached */
      SIMPLE_ERROR("Illegal smm mode: {} for CLASP_STREAM_C_STREAM", smm);
      UNREACHABLE();
    }
    output = IOStreamStream_O::make(fn, fp, smm, byte_size, flags, external_format, temp_name, created);
    gc::As<IOStreamStream_sp>(output)->set_buffering_mode(byte_size ? kw::_sym_full : kw::_sym_line);
  } else {
    output = IOFileStream_O::make(fn, f, smm, byte_size, flags, external_format, temp_name, created);
  }
  if (smm == stream_mode_probe) {
    eval::funcall(cl::_sym_close, output);
  } else {
    output->_flags |= CLASP_STREAM_MIGHT_SEEK;
    //            si_set_finalizer(output, _lisp->_true());
    /* Set file pointer to the correct position */
    if (appending) {
      stream_set_position(output, nil<T_O>());
    } else {
      stream_set_position(output, make_fixnum(0));
    }
  }
  return output;
}

CL_LAMBDA("filename &key (direction :input) (element-type 'base-char) (if-exists nil iesp) (if-does-not-exist nil idnesp) (external-format :default) (cstream T)");
CL_DECLARE();
CL_DOCSTRING(R"dx(open)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__open(T_sp filename, T_sp direction, T_sp element_type, T_sp if_exists, bool iesp, T_sp if_does_not_exist,
                       bool idnesp, T_sp external_format, T_sp cstream) {
  if (filename.nilp()) {
    TYPE_ERROR(filename, Cons_O::createList(cl::_sym_or, cl::_sym_string, cl::_sym_Pathname_O, cl::_sym_Stream_O));
  }
  T_sp strm;
  StreamMode smm;
  int flags = 0;
  gctools::Fixnum byte_size;
  /* INV: clasp_open_stream() checks types */
  if (direction == kw::_sym_input) {
    smm = stream_mode_input;
    if (!idnesp)
      if_does_not_exist = kw::_sym_error;
  } else if (direction == kw::_sym_output) {
    smm = stream_mode_output;
    if (!iesp)
      if_exists = kw::_sym_new_version;
    if (!idnesp) {
      if (if_exists == kw::_sym_overwrite || if_exists == kw::_sym_append)
        if_does_not_exist = kw::_sym_error;
      else
        if_does_not_exist = kw::_sym_create;
    }
  } else if (direction == kw::_sym_io) {
    smm = stream_mode_io;
    if (!iesp)
      if_exists = kw::_sym_new_version;
    if (!idnesp) {
      if (if_exists == kw::_sym_overwrite || if_exists == kw::_sym_append)
        if_does_not_exist = kw::_sym_error;
      else
        if_does_not_exist = kw::_sym_create;
    }
  } else if (direction == kw::_sym_probe) {
    smm = stream_mode_probe;
    if (!idnesp)
      if_does_not_exist = nil<T_O>();
  } else {
    FEerror("~S is an illegal DIRECTION for OPEN.", 1, direction.raw_());
    UNREACHABLE();
  }
  byte_size = clasp_normalize_stream_element_type(element_type);
  if (byte_size != 0) {
    external_format = nil<T_O>();
  }
  if (!cstream.nilp()) {
    flags |= CLASP_STREAM_C_STREAM;
  }
  strm = clasp_open_stream(filename, smm, if_exists, if_does_not_exist, byte_size, flags, external_format);
  return strm;
}

CL_LAMBDA(strm &key abort);
CL_DECLARE();
CL_DOCSTRING(R"dx(Lower-level version of cl:close)dx");
CL_DOCSTRING_LONG(
    R"dx(However, this won't be redefined by gray streams and will be available to call after cl:close is redefined by gray::redefine-cl-functions.)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__closeSTAR(T_sp strm, T_sp abort) { return stream_close(strm, abort); }

CL_LAMBDA(strm &key abort);
CL_DECLARE();
CL_DOCSTRING(R"doc(close)doc");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__close(T_sp strm, T_sp abort) { return core__closeSTAR(strm, abort); }

/**********************************************************************
 * BACKEND
 */

ListenResult FileStream_O::_fd_listen(int fileno) {
#ifdef CLASP_MS_WINDOWS_HOST
  HANDLE hnd = (HANDLE)_get_osfhandle(fileno);
  switch (GetFileType(hnd)) {
  case FILE_TYPE_CHAR: {
    DWORD dw, dw_read, cm;
    if (GetNumberOfConsoleInputEvents(hnd, &dw)) {
      unlikely_if(!GetConsoleMode(hnd, &cm)) FEwin32_error("GetConsoleMode() failed", 0);
      if (dw > 0) {
        PINPUT_RECORD recs = (PINPUT_RECORD)ALLIGNED_GC_MALLOC(sizeof(INPUT_RECORD) * dw);
        int i;
        unlikely_if(!PeekConsoleInput(hnd, recs, dw, &dw_read)) FEwin32_error("PeekConsoleInput failed()", 0);
        if (dw_read > 0) {
          if (cm & ENABLE_LINE_INPUT) {
            for (i = 0; i < dw_read; i++)
              if (recs[i].EventType == KEY_EVENT && recs[i].Event.KeyEvent.bKeyDown && recs[i].Event.KeyEvent.uChar.AsciiChar == 13)
                return listen_result_available;
          } else {
            for (i = 0; i < dw_read; i++)
              if (recs[i].EventType == KEY_EVENT && recs[i].Event.KeyEvent.bKeyDown && recs[i].Event.KeyEvent.uChar.AsciiChar != 0)
                return listen_result_available;
          }
        }
      }
      return listen_result_no_char;
    } else
      FEwin32_error("GetNumberOfConsoleInputEvents() failed", 0);
    break;
  }
  case FILE_TYPE_DISK:
    /* use regular file code below */
    break;
  case FILE_TYPE_PIPE: {
    DWORD dw;
    if (PeekNamedPipe(hnd, NULL, 0, NULL, &dw, NULL))
      return (dw > 0 ? listen_result_available : listen_result_no_char);
    else if (GetLastError() == ERROR_BROKEN_PIPE)
      return listen_result_eof;
    else
      FEwin32_error("PeekNamedPipe() failed", 0);
    break;
  }
  default:
    FEerror("Unsupported Windows file type: ~A", 1, make_fixnum(GetFileType(hnd)).raw_());
    break;
  }
#else
  /* Method 1: poll, see POLL(2)
     Method 2: select, see SELECT(2)
     Method 3: ioctl FIONREAD, see FILIO(4)
     Method 4: read a byte. Use non-blocking I/O if poll or select were not
               available. */
  int result;
#if defined(HAVE_POLL)
  struct pollfd fd = {fileno, POLLIN, 0};
restart_poll:
  result = poll(&fd, 1, 0);
  if (UNLIKELY(result < 0)) {
    if (errno == EINTR)
      goto restart_poll;
    goto listen_error;
  }
  if (fd.revents == 0) {
    return listen_result_no_char;
  }
  /* When read() returns a result without blocking, this can also be
     EOF! (Example: Linux and pipes.) We therefore refrain from simply
     doing  { return listen_result_available; }  and instead try methods
     3 and 4. */
#elif defined(HAVE_SELECT)
  fd_set fds;
  struct timeval tv = {0, 0};
  FD_ZERO(&fds);
  FD_SET(fileno, &fds);
restart_select:
  result = select(fileno + 1, &fds, NULL, NULL, &tv);
  if (UNLIKELY(result < 0)) {
    if (errno == EINTR)
      goto restart_select;
    if (errno != EBADF) /* UNIX_LINUX returns EBADF for files! */
      goto listen_error;
  } else if (result == 0) {
    return listen_result_no_char;
  }
#endif
#ifdef FIONREAD
  long c = 0;
  if (ioctl(fileno, FIONREAD, &c) < 0) {
    if (!((errno == ENOTTY) || IS_EINVAL))
      goto listen_error;
    return (c > 0) ? listen_result_available : listen_result_eof;
  }
#endif
#if !defined(HAVE_POLL) && !defined(HAVE_SELECT)
  int flags = fcntl(fd, F_GETFL, 0);
#endif
  int read_errno;
  cl_index b;
restart_read:
#if !defined(HAVE_POLL) && !defined(HAVE_SELECT)
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
  result = read(fileno, &b, 1);
  read_errno = errno;
#if !defined(HAVE_POLL) && !defined(HAVE_SELECT)
  fcntl(fd, F_SETFL, flags);
#endif
  if (result < 0) {
    if (read_errno == EINTR)
      goto restart_read;
    if (read_errno == EAGAIN || read_errno == EWOULDBLOCK)
      return listen_result_no_char;
    goto listen_error;
  }

  if (result == 0) {
    return listen_result_eof;
  }

  _byte_stack = Cons_O::createList(make_fixnum(b));
  return listen_result_available;
listen_error:
  file_libc_error(core::_sym_simpleStreamError, asSmartPtr(), "Error while listening to stream.", 0);
#endif
  return listen_result_unknown;
}

ListenResult IOStreamStream_O::_file_listen() {
  ListenResult aux;
  if (feof(_file))
    return listen_result_eof;
#ifdef FILE_CNT
  if (FILE_CNT(_file) > 0)
    return listen_result_available;
#endif
  aux = _fd_listen(fileno(_file));
  if (aux != listen_result_unknown)
    return aux;
  /* This code is portable, and implements the expected behavior for regular files.
            It will fail on noninteractive streams. */
  {
    /* regular file */
    clasp_off_t old_pos = clasp_ftello(_file), end_pos;
    unlikely_if(old_pos < 0) {
      file_libc_error(core::_sym_simpleFileError, asSmartPtr(), "Unable to check file position in SEEK_END", 0);
    }
    unlikely_if(clasp_fseeko(_file, 0, SEEK_END) != 0) {
      file_libc_error(core::_sym_simpleFileError, asSmartPtr(), "Unable to check file position in SEEK_END", 0);
    }
    end_pos = clasp_ftello(_file);
    unlikely_if(clasp_fseeko(_file, old_pos, SEEK_SET) != 0)
        file_libc_error(core::_sym_simpleFileError, asSmartPtr(), "Unable to check file position in SEEK_SET", 0);
    return (end_pos > old_pos ? listen_result_available : listen_result_eof);
  }
  return listen_result_no_char;
}

T_sp clasp_off_t_to_integer(clasp_off_t offset) {
  T_sp output;
  if (sizeof(clasp_off_t) == sizeof(gctools::Fixnum)) {
    output = Integer_O::create((gctools::Fixnum)offset);
  } else if (offset <= MOST_POSITIVE_FIXNUM) {
    output = make_fixnum((gctools::Fixnum)offset);
  } else {
    IMPLEMENT_MEF("Handle breaking converting clasp_off_t to Bignum");
  }
  return output;
}

clasp_off_t clasp_integer_to_off_t(T_sp offset) {
  clasp_off_t output = 0;
  if (sizeof(clasp_off_t) == sizeof(gctools::Fixnum)) {
    output = clasp_to_integral<clasp_off_t>(offset);
  } else if (core__fixnump(offset)) {
    output = clasp_to_integral<clasp_off_t>(offset);
  } else if (core__bignump(offset)) {
    IMPLEMENT_MEF("Implement convert Bignum to clasp_off_t");
  } else {
    //	ERR:
    FEerror("Not a valid file offset: ~S", 1, offset.raw_());
  }
  return output;
}

/**********************************************************************
 * ERROR MESSAGES
 */

void not_a_file_stream(T_sp strm) {
  cl__error(cl::_sym_simpleTypeError,
            Cons_O::createList(kw::_sym_format_control, SimpleBaseString_O::make("~A is not a file stream"),
                               kw::_sym_format_arguments, Cons_O::createList(strm), kw::_sym_expected_type, cl::_sym_FileStream_O,
                               kw::_sym_datum, strm));
}

void not_an_input_stream(T_sp strm) {
  cl__error(cl::_sym_simpleTypeError,
            Cons_O::createList(kw::_sym_format_control, SimpleBaseString_O::make("~A is not an input stream"),
                               kw::_sym_format_arguments, Cons_O::createList(strm), kw::_sym_expected_type,
                               Cons_O::createList(cl::_sym_satisfies, cl::_sym_input_stream_p), kw::_sym_datum, strm));
}

void not_an_output_stream(T_sp strm) {
  cl__error(cl::_sym_simpleTypeError,
            Cons_O::createList(kw::_sym_format_control, SimpleBaseString_O::make("~A is not an output stream"),
                               kw::_sym_format_arguments, Cons_O::createList(strm), kw::_sym_expected_type,
                               Cons_O::createList(cl::_sym_satisfies, cl::_sym_output_stream_p), kw::_sym_datum, strm));
}

void not_a_character_stream(T_sp s) {
  cl__error(cl::_sym_simpleTypeError,
            Cons_O::createList(kw::_sym_format_control, SimpleBaseString_O::make("~A is not a character stream"),
                               kw::_sym_format_arguments, Cons_O::createList(s), kw::_sym_expected_type, cl::_sym_character,
                               kw::_sym_datum, cl__stream_element_type(s)));
}

void not_a_binary_stream(T_sp s) {
  cl__error(cl::_sym_simpleTypeError,
            Cons_O::createList(kw::_sym_format_control, SimpleBaseString_O::make("~A is not a binary stream"),
                               kw::_sym_format_arguments, Cons_O::createList(s), kw::_sym_expected_type, cl::_sym_Integer_O,
                               kw::_sym_datum, cl__stream_element_type(s)));
}

void cannot_close(T_sp stream) { file_libc_error(core::_sym_simpleFileError, stream, "Stream cannot be closed", 0); }

void unread_error(T_sp s) { CEerror(_lisp->_true(), "Error when using UNREAD-CHAR on stream ~D", 1, s.raw_()); }

void unread_twice(T_sp s) { CEerror(_lisp->_true(), "Used UNREAD-CHAR twice on stream ~D", 1, s.raw_()); }

void maybe_clearerr(T_sp strm) {
  IOStreamStream_sp s = strm.asOrNull<IOStreamStream_O>();
  if (s && s->_file)
    clearerr(s->_file);
}

int restartable_io_error(T_sp strm, const char* s) {
  cl_env_ptr the_env = clasp_process_env();
  volatile int old_errno = errno;
  /* clasp_disable_interrupts(); ** done by caller */
  maybe_clearerr(strm);
  clasp_enable_interrupts_env(the_env);
  if (old_errno == EINTR) {
    return 1;
  } else {
    String_sp temp = SimpleBaseString_O::make(std::string(s, strlen(s)));
    file_libc_error(core::_sym_simpleStreamError, strm, "C operation (~A) signaled an error.", 1, temp.raw_());
    return 0;
  }
}

void io_error(T_sp strm) {
  cl_env_ptr the_env = clasp_process_env();
  /* clasp_disable_interrupts(); ** done by caller */
  maybe_clearerr(strm);
  clasp_enable_interrupts_env(the_env);
  file_libc_error(core::_sym_simpleStreamError, strm, "Read or write operation signaled an error", 0);
}

void wrong_file_handler(T_sp strm) { FEerror("Internal error: stream ~S has no valid C file handler.", 1, strm.raw_()); }

#ifdef CLASP_UNICODE
SYMBOL_EXPORT_SC_(ExtPkg, encoding_error);
cl_index encoding_error(T_sp stream, unsigned char* buffer, claspCharacter c) {
  T_sp code = eval::funcall(ext::_sym_encoding_error, stream, stream_external_format(stream), Integer_O::create((Fixnum)c));
  if (code.nilp()) {
    /* Output nothing */
    return 0;
  } else {
    /* Try with supplied character */
    return gctools::As<FileStream_sp>(stream)->encode(buffer, clasp_as_claspCharacter(gc::As<Character_sp>(code)));
  }
}

SYMBOL_EXPORT_SC_(ExtPkg, decoding_error);
claspCharacter decoding_error(T_sp stream, unsigned char** buffer, int length, unsigned char* buffer_end) {
  T_sp octets = nil<T_O>(), code;
  for (; length > 0; length--) {
    octets = Cons_O::create(make_fixnum(*((*buffer)++)), octets);
  }
  code = eval::funcall(ext::_sym_decoding_error, stream, stream_external_format(stream), octets);
  if (code.nilp()) {
    /* Go for next character */
    return gctools::As<FileStream_sp>(stream)->decode(buffer, buffer_end);
  } else {
    /* Return supplied character */
    return clasp_as_claspCharacter(gc::As<Character_sp>(code));
  }
}
#endif

#if defined(ECL_WSOCK)
void wsock_error(const char* err_msg, T_sp strm) {
  char* msg;
  T_sp msg_obj;
  /* clasp_disable_interrupts(); ** done by caller */
  {
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, 0, WSAGetLastError(), 0, (void*)&msg, 0, NULL);
    msg_obj = make_base_string_copy(msg);
    LocalFree(msg);
  }
  clasp_enable_interrupts();
  FEerror(err_msg, 2, strm.raw_(), msg_obj.raw_());
}
#endif

CL_LAMBDA(stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(streamLinenumber)dx");
DOCGROUP(clasp);
CL_DEFUN int core__stream_linenumber(T_sp tstream) { return clasp_input_lineno(tstream); };

CL_LAMBDA(stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(streamColumn)dx");
DOCGROUP(clasp);
CL_DEFUN int core__stream_column(T_sp tstream) { return clasp_input_column(tstream); };
}; // namespace core

namespace core {
void clasp_write_characters(const char* buf, int sz, T_sp strm) {
  for (int i(0); i < sz; ++i) {
    stream_write_char(strm, buf[i]);
  }
}

void clasp_write_string(const string& str, T_sp strm) { clasp_write_characters(str.c_str(), str.size(), strm); }

void clasp_writeln_string(const string& str, T_sp strm) {
  clasp_write_string(str, strm);
  clasp_terpri(strm);
  stream_finish_output(strm);
}

size_t clasp_input_filePos(T_sp strm) {
  T_sp position = stream_position(strm);
  if (position == kw::_sym_start)
    return 0;
  else if (position == kw::_sym_end) {
    SIMPLE_ERROR("Handle clasp_input_filePos getting :end");
  } else if (position.nilp()) {
    return 0;
    //	    SIMPLE_ERROR("Stream does not have file position");
  } else if (position.fixnump()) {
    return unbox_fixnum(gc::As<Fixnum_sp>(position));
  }
  return 0;
}

int clasp_input_lineno(T_sp stream) {
  AnsiStream_sp ansi_stream = stream.asOrNull<AnsiStream_O>();
  return ansi_stream ? ansi_stream->_input_cursor._line_number : 0;
}

int clasp_input_column(T_sp stream) {
  AnsiStream_sp ansi_stream = stream.asOrNull<AnsiStream_O>();
  return ansi_stream ? ansi_stream->_input_cursor._column : 0;
}

CL_LAMBDA(stream file line-offset positional-offset);
CL_DECLARE();
CL_DOCSTRING(R"dx(sourcePosInfo)dx");
DOCGROUP(clasp);
CL_DEFUN SourcePosInfo_sp core__input_stream_source_pos_info(T_sp strm, FileScope_sp sfi, size_t line_offset, size_t pos_offset) {
  strm = coerce::inputStreamDesignator(strm);
  size_t filePos = clasp_input_filePos(strm) + pos_offset;
  uint lineno, column;
  lineno = clasp_input_lineno(strm) + line_offset;
  column = clasp_input_column(strm);
  SourcePosInfo_sp spi = SourcePosInfo_O::create(sfi->fileHandle(), filePos, lineno, column);
  return spi;
}

// Called from LOAD
SourcePosInfo_sp clasp_simple_input_stream_source_pos_info(T_sp strm) {
  strm = coerce::inputStreamDesignator(strm);
  FileScope_sp sfi = clasp_input_source_file_info(strm);
  size_t filePos = clasp_input_filePos(strm);
  uint lineno, column;
  lineno = clasp_input_lineno(strm);
  column = clasp_input_column(strm);
  SourcePosInfo_sp spi = SourcePosInfo_O::create(sfi->fileHandle(), filePos, lineno, column);
  return spi;
}

FileScope_sp clasp_input_source_file_info(T_sp strm) { return gc::As<FileScope_sp>(core__file_scope(stream_pathname(strm))); }
}; // namespace core

namespace core {

AnsiStream_O::~AnsiStream_O() { close(nil<T_O>()); };

cl_index FileStream_O::consume_byte_stack(unsigned char* c, cl_index n) {
  cl_index out = 0;
  while (n) {
    if (_byte_stack.nilp())
      return out + read_byte8(c, n);
    *(c++) = (oCar(_byte_stack)).unsafe_fixnum();
    out++;
    n--;
    _byte_stack = oCdr(_byte_stack);
  }
  return out;
}

int AnsiStream_O::restartable_io_error(const char* s) {
  cl_env_ptr the_env = clasp_process_env();
  volatile int old_errno = errno;
  /* clasp_disable_interrupts(); ** done by caller */
  maybe_clearerr(this->asSmartPtr());
  clasp_enable_interrupts_env(the_env);
  if (old_errno == EINTR) {
    return 1;
  } else {
    String_sp temp = SimpleBaseString_O::make(std::string(s, strlen(s)));
    file_libc_error(core::_sym_simpleStreamError, this->asSmartPtr(), "C operation (~A) signaled an error.", 1, temp.raw_());
    return 0;
  }
}

cl_index AnsiStream_O::write_byte8(unsigned char* c, cl_index n) {
  not_an_output_stream(asSmartPtr());
  return 0;
}

cl_index AnsiStream_O::read_byte8(unsigned char* c, cl_index n) {
  not_an_input_stream(asSmartPtr());
  return 0;
}

void AnsiStream_O::write_byte(T_sp c) { not_an_output_stream(asSmartPtr()); }

T_sp AnsiStream_O::read_byte() {
  not_an_input_stream(asSmartPtr());
  return nil<T_O>();
}

claspCharacter AnsiStream_O::read_char() {
  not_an_input_stream(asSmartPtr());
  return EOF;
}

claspCharacter AnsiStream_O::write_char(claspCharacter c) {
  not_an_output_stream(asSmartPtr());
  return EOF;
}

void AnsiStream_O::unread_char(claspCharacter c) { not_an_input_stream(asSmartPtr()); }

claspCharacter AnsiStream_O::peek_char() {
  claspCharacter out = read_char();
  if (out != EOF)
    unread_char(out);
  return out;
}

cl_index AnsiStream_O::read_vector(T_sp data, cl_index start, cl_index end) {
  if (start >= end)
    return start;
  Vector_sp vec = gc::As<Vector_sp>(data);
  T_sp expected_type = element_type();
  if (expected_type == cl::_sym_base_char || expected_type == cl::_sym_character) {
    for (; start < end; start++) {
      claspCharacter c = read_char();
      if (c == EOF)
        break;
      vec->rowMajorAset(start, clasp_make_character(c));
    }
  } else {
    for (; start < end; start++) {
      T_sp x = read_byte();
      if (x.nilp())
        break;
      vec->rowMajorAset(start, x);
    }
  }
  return start;
}

cl_index AnsiStream_O::write_vector(T_sp data, cl_index start, cl_index end) {
  if (start >= end)
    return start;
  Vector_sp vec = gc::As<Vector_sp>(data);
  T_sp elementType = vec->element_type();
  if (elementType == cl::_sym_base_char ||
#ifdef CLASP_UNICODE
      elementType == cl::_sym_character ||
#endif
      (elementType == cl::_sym_T && cl__characterp(vec->rowMajorAref(0)))) {
    for (; start < end; start++) {
      write_char(clasp_as_claspCharacter(gc::As<Character_sp>((vec->rowMajorAref(start)))));
    }
  } else {
    for (; start < end; start++) {
      write_byte(vec->rowMajorAref(start));
    }
  }
  return start;
}

ListenResult AnsiStream_O::listen() {
  not_an_input_stream(asSmartPtr());
  return listen_result_eof;
}

void AnsiStream_O::clear_input() { not_an_input_stream(asSmartPtr()); }

void AnsiStream_O::clear_output() { not_an_output_stream(asSmartPtr()); }

void AnsiStream_O::finish_output() { not_an_output_stream(asSmartPtr()); }

void AnsiStream_O::force_output() { not_an_output_stream(asSmartPtr()); }

bool AnsiStream_O::open_p() const { return _open; }

bool AnsiStream_O::input_p() const { return false; }

bool AnsiStream_O::output_p() const { return false; }

bool AnsiStream_O::interactive_p() const { return false; }

T_sp AnsiStream_O::element_type() const { return _lisp->_true(); }

T_sp AnsiStream_O::set_element_type(T_sp type) {
  FEerror("Cannot change element type of stream ~A", 0, this);
  return type;
}

T_sp AnsiStream_O::external_format() const { return kw::_sym_default; }

T_sp AnsiStream_O::set_external_format(T_sp external_format) {
  FEerror("Cannot change external format of stream ~A", 0, this);
  return external_format;
}

T_sp AnsiStream_O::length() {
  not_a_file_stream(asSmartPtr());
  return nil<T_O>();
}

T_sp AnsiStream_O::position() { return nil<T_O>(); }

T_sp AnsiStream_O::set_position(T_sp pos) { return nil<T_O>(); }

T_sp AnsiStream_O::string_length(T_sp string) {
  not_a_file_stream(asSmartPtr());
  return nil<T_O>();
}

int AnsiStream_O::column() const { return _output_column; }

int AnsiStream_O::set_column(int column) { return _output_column = column; }

int AnsiStream_O::input_handle() { return -1; }

int AnsiStream_O::output_handle() { return -1; }

T_sp AnsiStream_O::close(T_sp _abort) {
  _open = false;
  return _lisp->_true();
}

T_sp AnsiStream_O::pathname() const {
  // not_a_file_stream(asSmartPtr());
  return nil<T_O>();
}

T_sp AnsiStream_O::truename() const {
  // not_a_file_stream(asSmartPtr());
  return nil<T_O>();
}

int AnsiStream_O::lineno() const { return 0; };

void StringOutputStream_O::fill(const string& data) { StringPushStringCharStar(this->_contents, data.c_str()); }

/*! Get the contents and reset them */
void StringOutputStream_O::clear() {
  _contents = Str8Ns_O::createBufferString(STRING_OUTPUT_STREAM_DEFAULT_SIZE);
  _output_column = 0;
};

/*! Get the contents and reset them */
String_sp StringOutputStream_O::getAndReset() {
  String_sp contents = _contents;
  _contents = Str8Ns_O::createBufferString(STRING_OUTPUT_STREAM_DEFAULT_SIZE);
  _output_column = 0;
  return contents;
};

bool FileStream_O::has_file_position() const { return false; }

bool IOFileStream_O::has_file_position() const {
  int fd = fileDescriptor();
  return clasp_has_file_position(fd);
}

string FileStream_O::__repr__() const {
  stringstream ss;
  ss << "#<" << this->_instanceClass()->_classNameAsString();
  ss << " " << _rep_(pathname());
  if (has_file_position()) {
    if (_open) {
      ss << " file-pos ";
      ss << _rep_(stream_position(this->asSmartPtr()));
    }
  }
  ss << ">";
  return ss.str();
}

string SynonymStream_O::__repr__() const {
  stringstream ss;
  ss << "#<" << this->_instanceClass()->_classNameAsString() << " ";
  ss << _rep_(this->_symbol) << ">";
  return ss.str();
}

T_sp StringInputStream_O::make(const string& str) {
  String_sp s = str_create(str);
  return cl__make_string_input_stream(s, make_fixnum(0), nil<T_O>());
}

string StringInputStream_O::peerFrom(size_t start, size_t len) {
  if (start >= this->_input_limit) {
    SIMPLE_ERROR("Cannot peer beyond the input limit at {}", this->_input_limit);
  }
  size_t remaining = this->_input_limit - start;
  len = MIN(len, remaining);
  stringstream ss;
  for (size_t i = 0; i < len; ++i) {
    ss << (char)(this->_contents->rowMajorAref(start + i).unsafe_character() & 0xFF);
  }
  return ss.str();
}
string StringInputStream_O::peer(size_t len) {
  size_t remaining = this->_input_limit - this->_input_position;
  len = MIN(len, remaining);
  stringstream ss;
  for (size_t i = 0; i < len; ++i) {
    ss << (char)(this->_contents->rowMajorAref(this->_input_position + i).unsafe_character() & 0x7F);
  }
  return ss.str();
}

CL_LAMBDA(strm &optional (eof-error-p t) eof-value);
CL_DECLARE();
CL_DOCSTRING(R"dx(readByte)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__read_byte(T_sp strm, T_sp eof_error_p, T_sp eof_value) {
  // Should signal an error of type type-error if stream is not a stream.
  // Should signal an error of type error if stream is not a binary input stream.
  if (strm.nilp())
    TYPE_ERROR(strm, cl::_sym_Stream_O);
  // as a side effect verifies that strm is really a stream.
  T_sp elt_type = stream_element_type(strm);
  if (elt_type == cl::_sym_character || elt_type == cl::_sym_base_char)
    SIMPLE_ERROR("Not a binary stream");

  T_sp c = stream_read_byte(strm);

  if (!c.nilp())
    return c;

  if (eof_error_p.nilp())
    return eof_value;

  ERROR_END_OF_FILE(strm);
}

CL_LAMBDA(&optional peek-type strm (eof-errorp t) eof-value recursivep);
CL_DECLARE();
CL_DOCSTRING(R"dx(peekChar)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__peek_char(T_sp peek_type, T_sp strm, T_sp eof_errorp, T_sp eof_value, T_sp recursive_p) {
  strm = coerce::inputStreamDesignator(strm);
  if (!stream_input_p(strm))
    SIMPLE_ERROR("Not input-stream");
  if (peek_type.nilp()) {
    int c = stream_peek_char(strm);
    if (c == EOF)
      goto HANDLE_EOF;
    return clasp_make_character(stream_peek_char(strm));
  }
  if (cl__characterp(peek_type)) {
    claspCharacter looking_for = clasp_as_claspCharacter(gc::As<Character_sp>(peek_type));
    while (1) {
      int c = stream_peek_char(strm);
      if (c == EOF)
        goto HANDLE_EOF;
      if (c == looking_for)
        return clasp_make_character(c);
      stream_read_char(strm);
    }
  }
  // Now peek_type is true - this means skip whitespace until the first non-whitespace character
  if (peek_type != _lisp->_true()) {
    SIMPLE_ERROR("Illegal first argument for PEEK-CHAR {}", _rep_(peek_type));
  } else {
    T_sp readtable = _lisp->getCurrentReadTable();
    while (1) {
      int c = stream_peek_char(strm);
      if (c == EOF)
        goto HANDLE_EOF;
      Character_sp charc = clasp_make_character(c);
      if (core__syntax_type(readtable, charc) != kw::_sym_whitespace)
        return charc;
      stream_read_char(strm);
    }
  }
HANDLE_EOF:
  if (eof_errorp.isTrue())
    ERROR_END_OF_FILE(strm);
  return eof_value;
}

CL_LAMBDA(&optional strm (eof-error-p t) eof-value recursive-p);
CL_DECLARE();
CL_DOCSTRING(R"dx(readChar)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__read_char(T_sp strm, T_sp eof_error_p, T_sp eof_value, T_sp recursive_p) {
  strm = coerce::inputStreamDesignator(strm);
  int c = stream_read_char(strm);
  if (c == EOF) {
    LOG("Hit eof");
    if (!eof_error_p.isTrue()) {
      LOG("Returning eof_value[{}]", _rep_(eof_value));
      return eof_value;
    }
    ERROR_END_OF_FILE(strm);
  }
  LOG("Read and returning char[{}]", c);
  return clasp_make_character(c);
}

CL_LAMBDA(&optional stream (eof-error-p t) eof-value recursive-p);
CL_DECLARE();
CL_DOCSTRING(R"dx(readCharNoHang)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__read_char_no_hang(T_sp stream, T_sp eof_error_p, T_sp eof_value, T_sp recursive_p) {
  stream = coerce::inputStreamDesignator(stream);
  AnsiStream_sp ansi_stream = stream.asOrNull<AnsiStream_O>();

  if (ansi_stream) {
    int f = ansi_stream->listen();
    if (f == listen_result_available) {
      int c = ansi_stream->read_char();
      if (c != EOF)
        return clasp_make_standard_character(c);
    } else if (f == listen_result_no_char) {
      return nil<T_O>();
    }
  } else {
    T_sp output = eval::funcall(gray::_sym_stream_read_char_no_hang, stream);
    if (output != kw::_sym_eof)
      return output;
  }

  if (eof_error_p.nilp())
    return eof_value;
  ERROR_END_OF_FILE(stream);
}

CL_LAMBDA(content &optional (eof-error-p t) eof-value &key (start 0) end preserve-whitespace);
CL_DECLARE();
CL_DOCSTRING(R"dx(read_from_string)dx");
DOCGROUP(clasp);
CL_DEFUN T_mv cl__read_from_string(String_sp content, T_sp eof_error_p, T_sp eof_value, Fixnum_sp start, T_sp end,
                                   T_sp preserve_whitespace) {
  ASSERT(cl__stringp(content));
  bool eofErrorP = eof_error_p.isTrue();
  int istart = clasp_to_int(start);
  int iend;
  if (end.nilp())
    iend = content->get_std_string().size();
  else
    iend = clasp_to_int(gc::As<Fixnum_sp>(end));
  StringInputStream_sp sin =
      gc::As_unsafe<StringInputStream_sp>(StringInputStream_O::make(content->get_std_string().substr(istart, iend - istart)));
  if (iend - istart == 0) {
    if (eofErrorP) {
      ERROR_END_OF_FILE(sin);
    } else {
      return (Values(eof_value, _lisp->_true()));
    }
  }
  LOG("Seeking to position: {}", start);
  LOG("Character at position[{}] is[{}/{}]", sin->tell(), (char)sin->peek_char(), (int)sin->peek_char());
  T_sp res;
  if (preserve_whitespace.isTrue()) {
    res = cl__read_preserving_whitespace(sin, nil<T_O>(), unbound<T_O>(), nil<T_O>());
  } else {
    res = cl__read(sin, nil<T_O>(), unbound<T_O>(), nil<T_O>());
  }
  if (res.unboundp()) {
    if (eofErrorP) {
      ERROR_END_OF_FILE(sin);
    } else {
      return (Values(eof_value, _lisp->_true()));
    }
  }
  return (Values(res, stream_position(sin)));
}

CL_LAMBDA(&optional input-stream (eof-error-p t) eof-value recursive-p);
CL_DECLARE();
CL_DOCSTRING(R"dx(See clhs)dx");
DOCGROUP(clasp);
CL_DEFUN T_mv cl__read_line(T_sp sin, T_sp eof_error_p, T_sp eof_value, T_sp recursive_p) {
  // TODO Handle encodings from sin - currently only Str8Ns is supported
  bool eofErrorP = eof_error_p.isTrue();
  sin = coerce::inputStreamDesignator(sin);
  AnsiStream_sp stream = sin.asOrNull<AnsiStream_O>();

  if (!stream) {
    T_mv results = eval::funcall(gray::_sym_stream_read_line, sin);
    MultipleValues& mvn = core::lisp_multipleValues();
    if (mvn.second(results.number_of_values()).isTrue() && (gc::As<core::String_sp>(results)->length() == 0)) {
      if (eof_error_p.notnilp()) {
        ERROR_END_OF_FILE(sin);
      } else {
        return Values(eof_value, _lisp->_true());
      }
    } else
      return results;
  }

  // Now we have an ANSI stream. Get read_char so we don't need to dispatch every iteration.
  // This is the second return value.
  T_sp missing_newline_p = nil<T_O>();
  // We set things up so that we accumulate a bytestring when possible, and revert to a real
  // character string if we hit multibyte characters.
  bool small = true;
  Str8Ns_sp sbuf_small = _lisp->get_Str8Ns_buffer_string();
  StrWNs_sp sbuf_wide;
  // Read loop
  while (1) {
    claspCharacter cc = stream->read_char();
    if (cc == EOF) { // hit end of file
      missing_newline_p = _lisp->_true();
      if (small) { // have a bytestring
        if (sbuf_small->length() > 0)
          break;                                       // we've read something - return it.
        else if (eofErrorP) {                          // we need to signal an error.
          _lisp->put_Str8Ns_buffer_string(sbuf_small); // return our buffer first
          ERROR_END_OF_FILE(sin);
        } else { // return the eof value.
          _lisp->put_Str8Ns_buffer_string(sbuf_small);
          return Values(eof_value, missing_newline_p);
        }
      } else
        break; // Otherwise we have a wide string- this implies we've read something.
    } else {   // have a real character
      if (!clasp_base_char_p(cc)) {
        // wide character.
        // NOTE: We assume that wide characters are not newlines.
        // In unicode this is false, e.g. U+2028 LINE SEPARATOR.
        // However, CLHS specifies #\Newline as the only newline. Maybe look at this more.
        if (small) {
          // We've read our first wide character - set up a wide buffer to use now
          small = false;
          sbuf_wide = _lisp->get_StrWNs_buffer_string();
          // Extend the wide buffer if necessary
          if (sbuf_wide->arrayTotalSize() < sbuf_small->length())
            sbuf_wide->resize(sbuf_small->length());
          // copy in the small buffer, then release the small buffer
          sbuf_wide->unsafe_setf_subseq(0, sbuf_small->length(), sbuf_small->asSmartPtr());
          sbuf_wide->fillPointerSet(sbuf_small->length());
          _lisp->put_Str8Ns_buffer_string(sbuf_small);
        }
        // actually put in the wide character
        sbuf_wide->vectorPushExtend(cc);
      } else if (cc == '\n')
        break; // hit a newline, get ready to return a result
      else if (cc == '\r') {
        // Treat a CR or CRLF as a newline.
        if (stream->peek_char() == '\n')
          stream->read_char(); // lose any LF first tho
        break;
      } else { // ok, we have a real non-newline character. accumulate.
        if (small)
          sbuf_small->vectorPushExtend(cc);
        else
          sbuf_wide->vectorPushExtend(cc);
      }
    }
  } // while(1)
  // We've accumulated a line. Copy it into a simple string, release the buffer, and return.
  LOG("Read line result -->[{}]", sbuf.str());
  if (small) {
    T_sp result = cl__copy_seq(sbuf_small);
    _lisp->put_Str8Ns_buffer_string(sbuf_small);
    return Values(result, missing_newline_p);
  } else {
    T_sp result = cl__copy_seq(sbuf_wide);
    _lisp->put_StrWNs_buffer_string(sbuf_wide);
    return Values(result, missing_newline_p);
  }
}

void clasp_terpri(T_sp stream) {
  stream = coerce::outputStreamDesignator(stream);

  AnsiStream_sp ansi_stream = stream.asOrNull<AnsiStream_O>();
  if (ansi_stream) {
    ansi_stream->write_char('\n');
    ansi_stream->force_output();
  } else
    eval::funcall(gray::_sym_stream_terpri, stream);
}

CL_LAMBDA(&optional output-stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(Send a newline to the output stream)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__terpri(T_sp outputStreamDesig) {
  // outputStreamDesign in clasp_terpri
  clasp_terpri(outputStreamDesig);
  return nil<T_O>();
};

bool clasp_freshLine(T_sp stream) {
  stream = coerce::outputStreamDesignator(stream);

  AnsiStream_sp ansi_stream = stream.asOrNull<AnsiStream_O>();
  if (ansi_stream) {
    if (ansi_stream->column() > 0) {
      ansi_stream->write_char('\n');
      ansi_stream->force_output();
      return true;
    }
    return false;
  }

  return T_sp(eval::funcall(gray::_sym_stream_fresh_line, stream)).notnilp();
}

CL_LAMBDA(&optional outputStream);
CL_DECLARE();
CL_DOCSTRING(R"dx(freshLine)dx");
DOCGROUP(clasp);
CL_DEFUN bool cl__fresh_line(T_sp outputStreamDesig) {
  // outputStreamDesignator in clasp_freshLine
  return clasp_freshLine(outputStreamDesig);
};

CL_LAMBDA(string &optional (output-stream cl:*standard-output*) &key (start 0) end);
CL_DECLARE();
CL_DOCSTRING(R"dx(writeString)dx");
CL_LISPIFY_NAME("cl:write-string");
DOCGROUP(clasp);
CL_DEFUN String_sp clasp_writeString(String_sp str, T_sp stream, int istart, T_sp end) {
  stream = coerce::outputStreamDesignator(stream);

  if (!stream.isA<AnsiStream_O>())
    return eval::funcall(gray::_sym_stream_write_string, stream, str, make_fixnum(istart), end);

  /*
  Beware that we might have unicode characters in str (or a non simple string)
  Don't use clasp_write_characters, since that operates on chars and
  might fail miserably with unicode strings, see issue 1134

  Best to audit in clasp every use of c_str() or even get_std_string() to see whether
  Strings might be clobbered.

  Write-String is not specified to respect print_escape and print_readably
  */
  // Verify no OutOfBound Access
  size_t_pair p = sequenceStartEnd(cl::_sym_writeString, str->length(), istart, end);
  str->__writeString(p.start, p.end, stream);
  return str;
}

CL_LAMBDA(string &optional output-stream &key (start 0) end);
CL_DECLARE();
CL_DOCSTRING(R"dx(writeLine)dx");
DOCGROUP(clasp);
CL_DEFUN String_sp cl__write_line(String_sp str, T_sp stream, int istart, T_sp end) {
  clasp_writeString(str, stream, istart, end);
  clasp_terpri(stream);
  return str;
};

CL_LAMBDA(byte output-stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(writeByte)dx");
DOCGROUP(clasp);
CL_DEFUN Integer_sp cl__write_byte(Integer_sp byte, T_sp stream) {
  if (stream.nilp())
    TYPE_ERROR(stream, cl::_sym_Stream_O);
  // clhs in 21.2 says stream---a binary output stream, not mentioning a stream designator
  stream_write_byte(stream, byte);
  return (byte);
};

CL_LAMBDA(string &optional output-stream);
CL_DECLARE();
CL_DOCSTRING(R"dx(writeChar)dx");
DOCGROUP(clasp);
CL_DEFUN Character_sp cl__write_char(Character_sp chr, T_sp stream) {
  stream = coerce::outputStreamDesignator(stream);
  stream_write_char(stream, clasp_as_claspCharacter(chr));
  return chr;
};

CL_LAMBDA(&optional dstrm);
CL_DECLARE();
CL_DOCSTRING(R"dx(clearInput)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__clear_input(T_sp dstrm) {
  dstrm = coerce::inputStreamDesignator(dstrm);
  stream_clear_input(dstrm);
  return nil<T_O>();
}

CL_LAMBDA(&optional dstrm);
CL_DECLARE();
CL_DOCSTRING(R"dx(clearOutput)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__clear_output(T_sp dstrm) {
  dstrm = coerce::outputStreamDesignator(dstrm);
  stream_clear_output(dstrm);
  return nil<T_O>();
}

CL_LAMBDA(&optional dstrm);
CL_DECLARE();
CL_DOCSTRING(R"dx(listen)dx");
DOCGROUP(clasp);
CL_DEFUN bool cl__listen(T_sp strm) {
  strm = coerce::inputStreamDesignator(strm);
  int result = stream_listen(strm);
  if (result == listen_result_eof)
    return 0;
  else
    return result;
}

CL_LAMBDA(&optional strm);
CL_DECLARE();
CL_DOCSTRING(R"dx(force_output)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__force_output(T_sp ostrm) {
  ostrm = coerce::outputStreamDesignator(ostrm);
  stream_force_output(ostrm);
  return nil<T_O>();
};

CL_LAMBDA(&optional strm);
CL_DECLARE();
CL_DOCSTRING(R"dx(finish_output)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__finish_output(T_sp ostrm) {
  ostrm = coerce::outputStreamDesignator(ostrm);
  stream_finish_output(ostrm);
  return nil<T_O>();
};

CL_LAMBDA(char &optional strm);
CL_DECLARE();
CL_DOCSTRING(R"dx(unread_char)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__unread_char(Character_sp ch, T_sp dstrm) {
  dstrm = coerce::inputStreamDesignator(dstrm);
  stream_unread_char(dstrm, clasp_as_claspCharacter(ch));
  return nil<T_O>();
};

CL_LAMBDA(stream);
CL_DECLARE();
CL_DOCSTRING("Return the current column of the stream");
DOCGROUP(clasp);
CL_DEFUN T_sp core__file_column(T_sp strm) {
  strm = coerce::outputStreamDesignator(strm);
  return make_fixnum(stream_column(strm));
};

CL_LISPIFY_NAME("core:file-column");
CL_DOCSTRING("Set the column of the stream is that is meaningful for the stream.");
DOCGROUP(clasp);
CL_DEFUN_SETF T_sp core__setf_file_column(int column, T_sp strm) {
  strm = coerce::outputStreamDesignator(strm);
  return make_fixnum(stream_set_column(strm, column));
};

/*! Translated from ecl::si_do_write_sequence */
CL_LAMBDA(seq stream &key (start 0) end);
CL_DECLARE();
CL_DOCSTRING(R"dx(writeSequence)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp cl__write_sequence(T_sp seq, T_sp stream, Fixnum_sp fstart, T_sp tend) {
  stream = coerce::outputStreamDesignator(stream);
  if (!stream.isA<AnsiStream_O>()) {
    return eval::funcall(gray::_sym_stream_write_sequence, stream, seq, fstart, tend);
  }
  int limit = cl__length(seq);
  unlikely_if(!core__fixnump(fstart) || (unbox_fixnum(fstart) < 0) || (unbox_fixnum(fstart) > limit)) {
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_start, fstart, Integer_O::makeIntegerType(0, limit - 1));
  }
  int start = unbox_fixnum(fstart);
  int end;
  if (tend.notnilp()) {
    end = unbox_fixnum(gc::As<Fixnum_sp>(tend));
    unlikely_if(!core__fixnump(tend) || (end < 0) || (end > limit)) {
      ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_end, tend, Integer_O::makeIntegerType(0, limit - 1));
    }
  } else
    end = limit;
  if (end == start)
    return seq;
  else if (end < start) {
    // I don't believe that we can silently return seq, sbcl throws an error
    ERROR_WRONG_TYPE_KEY_ARG(cl::_sym_write_sequence, kw::_sym_end, tend, Integer_O::makeIntegerType(start, limit - 1));
  }
  if (cl__listp(seq)) {
    T_sp elt_type = cl__stream_element_type(stream);
    bool ischar = (elt_type == cl::_sym_base_char) || (elt_type == cl::_sym_character);
    T_sp s = cl__nthcdr(clasp_make_integer(start), seq);
    for (;; s = cons_cdr(s)) {
      if (start < end) {
        T_sp elt = oCar(s);
        if (ischar)
          stream_write_char(stream, clasp_as_claspCharacter(gc::As<Character_sp>(elt)));
        else
          stream_write_byte(stream, gc::As<Integer_sp>(elt));
        start++;
      } else {
        goto OUTPUT;
      }
    }
  } else {
    stream_write_vector(stream, gc::As<Vector_sp>(seq), start, end);
  }
OUTPUT:
  return seq;
}

T_sp clasp_openRead(T_sp sin) {
  String_sp filename = gc::As<String_sp>(cl__namestring(sin));
  StreamMode smm = stream_mode_input;
  T_sp if_exists = nil<T_O>();
  T_sp if_does_not_exist = nil<T_O>();
  gctools::Fixnum byte_size = 8;
  int flags = CLASP_STREAM_DEFAULT_FORMAT;
  T_sp external_format = nil<T_O>();
  if (filename.nilp()) {
    SIMPLE_ERROR("{} was called with NIL as the argument", __FUNCTION__);
  }
  T_sp strm = clasp_open_stream(filename, smm, if_exists, if_does_not_exist, byte_size, flags, external_format);
  return strm;
}

T_sp clasp_openWrite(T_sp path) {
  T_sp stream = eval::funcall(cl::_sym_open, path, kw::_sym_direction, kw::_sym_output, kw::_sym_if_exists, kw::_sym_supersede);
  return stream;
}

SYMBOL_EXPORT_SC_(ClPkg, open);
SYMBOL_EXPORT_SC_(KeywordPkg, direction);
SYMBOL_EXPORT_SC_(KeywordPkg, output);
SYMBOL_EXPORT_SC_(KeywordPkg, input);
SYMBOL_EXPORT_SC_(ClPkg, filePosition);
SYMBOL_EXPORT_SC_(ClPkg, readSequence);
SYMBOL_EXPORT_SC_(CorePkg, doReadSequence);
SYMBOL_EXPORT_SC_(ClPkg, read_from_string);
SYMBOL_EXPORT_SC_(ClPkg, read_line);
SYMBOL_EXPORT_SC_(ClPkg, terpri);
SYMBOL_EXPORT_SC_(ClPkg, freshLine);
SYMBOL_EXPORT_SC_(ClPkg, writeString);
SYMBOL_EXPORT_SC_(ClPkg, writeLine);
SYMBOL_EXPORT_SC_(ClPkg, writeChar);
SYMBOL_EXPORT_SC_(ClPkg, clearInput);
SYMBOL_EXPORT_SC_(ClPkg, clearOutput);
SYMBOL_EXPORT_SC_(ClPkg, readByte);
SYMBOL_EXPORT_SC_(ClPkg, peekChar);
SYMBOL_EXPORT_SC_(ClPkg, readChar);
SYMBOL_EXPORT_SC_(ClPkg, readCharNoHang);
SYMBOL_EXPORT_SC_(ClPkg, force_output);
SYMBOL_EXPORT_SC_(ClPkg, finish_output);
SYMBOL_EXPORT_SC_(ClPkg, listen);
SYMBOL_EXPORT_SC_(ClPkg, unread_char);
SYMBOL_EXPORT_SC_(CorePkg, fileColumn);
SYMBOL_EXPORT_SC_(CorePkg, makeStringOutputStreamFromString);
SYMBOL_EXPORT_SC_(ClPkg, makeStringOutputStream);
SYMBOL_EXPORT_SC_(CorePkg, do_write_sequence);
SYMBOL_EXPORT_SC_(ClPkg, writeByte);
SYMBOL_EXPORT_SC_(ClPkg, input_stream_p);
SYMBOL_EXPORT_SC_(ClPkg, output_stream_p);
SYMBOL_EXPORT_SC_(ClPkg, interactive_stream_p);
SYMBOL_EXPORT_SC_(ClPkg, streamp);
SYMBOL_EXPORT_SC_(ClPkg, close);
SYMBOL_EXPORT_SC_(ClPkg, get_output_stream_string);
SYMBOL_EXPORT_SC_(CorePkg, streamLinenumber);
SYMBOL_EXPORT_SC_(CorePkg, streamColumn);
SYMBOL_EXPORT_SC_(ClPkg, synonymStreamSymbol);
SYMBOL_EXPORT_SC_(ExtPkg, file_stream_file_descriptor);

CL_DOCSTRING(R"dx(Use read to read characters if they are available - return (values num-read errno-or-nil))dx");
DOCGROUP(clasp);
CL_DEFUN T_mv core__read_fd(int filedes, SimpleBaseString_sp buffer) {
  size_t buffer_length = cl__length(buffer);
  unsigned char* buffer_data = &(*buffer)[0];
  while (1) {
    int num = read(filedes, buffer_data, buffer_length);
    if (!(num < 0 && errno == EINTR)) {
      if (num < 0) {
        return Values(make_fixnum(num), make_fixnum(errno));
      }
      return Values(make_fixnum(num), nil<T_O>());
    }
  }
};

// Read dense (6-bit) character strings into blobs of bytes
// See the reverse function denseWriteTo6Bit
void denseReadTo8Bit(T_sp stream, size_t charCount, unsigned char* buffer) {

#define CODING
#include "dense_specialized_array_dispatch.cc"
#undef CODING
  unsigned char reverse_coding[128];
  memset(reverse_coding, 0, 128);
  for (unsigned char ii = 0; ii < 64; ii++)
    reverse_coding[coding[ii]] = (unsigned char)ii;

  // Initialize variables to keep track of remaining bits from the previous character
  size_t total8bits = 0;
  size_t total6bits = 0;
  unsigned int remainingBits = 0;
  unsigned int previousBits = 0;
#if DEBUG_DENSE
  std::stringstream sout8;
  std::stringstream sout6;
#endif
  // Iterate through each character in the input stream
  for (size_t i = 0; i < charCount; ++i) {
    // Read and map the printable character back to a 6-bit value
    unsigned char printableChar = stream_read_char(stream);
    unsigned char sixBitValue = reverse_coding[printableChar];
    total6bits += 6;
#if DEBUG_DENSE
    std::bitset<6> bits6(sixBitValue);
    sout6 << bits6;
#endif

    // Combine the remaining bits from the previous character with the current 6-bit value
    unsigned int currentSixBitValue = (previousBits << 6) | sixBitValue;

    // Update the number of remaining bits
    remainingBits += 6;

    // Continue until there are at least 8 bits to extract
    while (remainingBits >= 8) {
      // Extract the next 8 bits
      unsigned int eightBitValue = (currentSixBitValue >> (remainingBits - 8)) & 0xFF;
#if DEBUG_DENSE
      std::bitset<8> bits(eightBitValue);
      sout8 << bits;
#endif

      // Add the 8-bit value to the result vector
      *buffer = (unsigned char)eightBitValue;
      total8bits += 8;
      buffer++;

      // Update variables for the next iteration
      remainingBits -= 8;
    }

    // Save the remaining bits for the next iteration
    previousBits = currentSixBitValue & ((1 << remainingBits) - 1);
  }
  // Subtract any trailing bits that weren't needed to generate the 8bit stream
  total6bits -= remainingBits;

#if DEBUG_DENSE
  printf("%s:%d:%s bit8 stream\n%s\n", __FILE__, __LINE__, __FUNCTION__, sout8.str().c_str());
  printf("%s\n%s:%d:%s bit6 stream\n", sout6.str().c_str(), __FILE__, __LINE__, __FUNCTION__);
#endif
  if (total8bits != total6bits) {
    SIMPLE_ERROR("total8bits {} must match total6bits {}", total8bits, total6bits);
  }
}

void read_array_readable_binary(T_sp stream, size_t num6bit, void* start, void* end) {
  size_t numBytes = (num6bit * 6) / 8;
  size_t size = ((const char*)end) - ((const char*)start);
  if (numBytes != size)
    SIMPLE_ERROR("Mismatch between the number of bytes {} from num6bit {}  and the size of the buffer {}", numBytes, size, num6bit);
  denseReadTo8Bit(stream, num6bit, (unsigned char*)start);
}

#define DISPATCH(_vtype_, _type_, _code_)                                                                                          \
  if (kind == _code_) {                                                                                                            \
    size_t elements = ((num6bit * 6) / 8) / sizeof(_type_);                                                                        \
    auto svf = SimpleVector_##_vtype_##_O::make(elements, 0.0);                                                                    \
    unsigned char* start = (unsigned char*)svf->rowMajorAddressOfElement_(0);                                                      \
    unsigned char* end = (unsigned char*)svf->rowMajorAddressOfElement_(elements);                                                 \
    read_array_readable_binary(stream, num6bit, start, end);                                                                       \
    claspCharacter c = stream_read_char(stream);                                                                                   \
    if (c != ' ')                                                                                                                  \
      SIMPLE_ERROR("Expected space at end of dense blob - got #\\{} ", c);                                                         \
    return svf;                                                                                                                    \
  }

CL_DEFUN T_sp core__read_dense_specialized_array(T_sp stream, size_t num6bit) {
  std::string kind = "  ";
  kind[0] = stream_read_char(stream);
  kind[1] = stream_read_char(stream);
//  printf("%s:%d:%s  num6bit = %lu  kind=%s\n", __FILE__, __LINE__, __FUNCTION__, num6bit, kind.c_str() );
#define DISPATCHES
#include "dense_specialized_array_dispatch.cc"
#undef DISPATCHES

#if 0
  if (kind == "sf") {
    size_t elements = (((num6bit*6) + 7)/8)/sizeof(float);
//    printf("%s:%d:%s elements = %lu\n", __FILE__, __LINE__, __FUNCTION__, elements );
    SimpleVector_float_sp svf = SimpleVector_float_O::make( elements, 0.0 );
    unsigned char* start = (unsigned char*)svf->rowMajorAddressOfElement_(0);
    unsigned char* end = (unsigned char*)svf->rowMajorAddressOfElement_(elements);
    read_array_readable_binary( stream, num6bit, start, end );
    claspCharacter c = stream_read_char(stream); // Eat the trailing space
    if (c != ' ') SIMPLE_ERROR("Expected space at end of dense blob - got #\\{} ", c );
    return svf;
  }
#endif
  SIMPLE_ERROR("Illegal dense type {}", kind);
}

CL_DOCSTRING(R"dx(Read 4 bytes and interpret them as a single float))dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__read_binary_single_float(T_sp stream) {
  unsigned char buffer[4];
  if (stream_read_byte8(stream, buffer, 4) < 4)
    return nil<T_O>();
  float val = *(float*)buffer;
  return make_single_float(val);
}

CL_DOCSTRING(R"dx(Write a single float as IEEE format in 4 contiguous bytes))dx");
DOCGROUP(clasp);
CL_DEFUN void core__write_ieee_single_float(T_sp stream, T_sp val) {
  if (!val.single_floatp())
    TYPE_ERROR(val, cl::_sym_single_float);
  unsigned char buffer[4];
  *(float*)buffer = val.unsafe_single_float();
  stream_write_byte8(stream, buffer, 4);
}

CL_DOCSTRING(R"dx(Write a C uint32 POD 4 contiguous bytes))dx");
DOCGROUP(clasp);
CL_DEFUN void core__write_c_uint32(T_sp stream, T_sp val) {
  if (!val.fixnump() || val.unsafe_fixnum() < 0 || val.unsafe_fixnum() >= (uint64_t)1 << 32) {
    TYPE_ERROR(val, ext::_sym_byte32);
  }
  unsigned char buffer[4];
  *(uint32_t*)buffer = val.unsafe_fixnum();
  stream_write_byte8(stream, buffer, 4);
}

CL_DOCSTRING(R"dx(Write a C uint16 POD 2 contiguous bytes))dx");
DOCGROUP(clasp);
CL_DEFUN void core__write_c_uint16(T_sp stream, T_sp val) {
  if (!val.fixnump() || val.unsafe_fixnum() < 0 || val.unsafe_fixnum() >= (uint64_t)1 << 16) {
    TYPE_ERROR(val, ext::_sym_byte16);
  }
  unsigned char buffer[2];
  *(uint16_t*)buffer = (uint16_t)val.unsafe_fixnum();
  stream_write_byte8(stream, buffer, 2);
}

CL_DOCSTRING(R"dx(Set filedescriptor to nonblocking)dx");
DOCGROUP(clasp);
CL_DEFUN void core__fcntl_non_blocking(int filedes) {
  int flags = fcntl(filedes, F_GETFL, 0);
  fcntl(filedes, F_SETFL, flags | O_NONBLOCK);
};

CL_DOCSTRING(R"dx(Close the file descriptor)dx");
DOCGROUP(clasp);
CL_DEFUN void core__close_fd(int filedes) { close(filedes); };

SYMBOL_EXPORT_SC_(KeywordPkg, seek_set);
SYMBOL_EXPORT_SC_(KeywordPkg, seek_cur);
SYMBOL_EXPORT_SC_(KeywordPkg, seek_end);

DOCGROUP(clasp);
CL_DEFUN int64_t core__lseek(int fd, int64_t offset, Symbol_sp whence) {
  int iwhence;
  if (whence == kw::_sym_seek_set) {
    iwhence = SEEK_SET;
  } else if (whence == kw::_sym_seek_cur) {
    iwhence = SEEK_CUR;
  } else if (whence == kw::_sym_seek_end) {
    iwhence = SEEK_END;
  } else {
    SIMPLE_ERROR("whence must be one of :seek-set, :seek-cur, :seek-end - it was {}", _rep_(whence));
  }
  size_t off = lseek(fd, offset, iwhence);
  return off;
};

/**********************************************************************
 * OTHER TOOLS
 */

CL_DEFUN T_sp core__copy_stream(T_sp in, T_sp out, T_sp wait) {
  claspCharacter c;
  if ((wait.nilp()) && !stream_listen(in)) {
    return nil<T_O>();
  }
  for (c = stream_read_char(in); c != EOF; c = stream_read_char(in)) {
    stream_write_char(out, c);
    if ((wait.nilp()) && !stream_listen(in)) {
      break;
    }
  }
  stream_force_output(out);
  return (c == EOF) ? _lisp->_true() : nil<T_O>();
}

void lisp_write(const std::string& s) { clasp_write_string(s); }

cl_index stream_write_byte8(T_sp stream, unsigned char* c, cl_index n) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->write_byte8(c, n);

  cl_index i;
  for (i = 0; i < n; i++) {
    T_sp byte = eval::funcall(gray::_sym_stream_write_byte, stream, make_fixnum(c[i]));
    if (!core__fixnump(byte))
      break;
  }
  return i;
}

cl_index stream_read_byte8(T_sp stream, unsigned char* c, cl_index n) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->read_byte8(c, n);

  cl_index i;
  for (i = 0; i < n; i++) {
    T_sp byte = eval::funcall(gray::_sym_stream_read_byte, stream);
    if (!core__fixnump(byte))
      break;
    c[i] = (byte).unsafe_fixnum();
  }
  return i;
}

void stream_write_byte(T_sp stream, T_sp c) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->write_byte(c);
  else
    eval::funcall(gray::_sym_stream_write_byte, stream, c);
}

T_sp stream_read_byte(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->read_byte();

  T_sp b = eval::funcall(gray::_sym_stream_read_byte, stream);
  if (b == kw::_sym_eof)
    b = nil<T_O>();
  return b;
}

claspCharacter stream_read_char(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->read_char();

  T_sp output = eval::funcall(gray::_sym_stream_read_char, stream);
  gctools::Fixnum value;
  if (cl__characterp(output))
    value = output.unsafe_character();
  else if (core__fixnump(output))
    value = (output).unsafe_fixnum();
  else if (output == nil<T_O>() || output == kw::_sym_eof)
    return EOF;
  else
    value = -1;
  unlikely_if(value < 0 || value > CHAR_CODE_LIMIT) FEerror("Unknown character ~A", 1, output.raw_());
  return value;
}

claspCharacter stream_write_char(T_sp stream, claspCharacter c) {
  if (!_lisp->_Roots._Started && !stream)
    return putchar(c);

  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->write_char(c);

  eval::funcall(gray::_sym_stream_write_char, stream, clasp_make_character(c));
  return c;
}

void stream_unread_char(T_sp stream, claspCharacter c) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->unread_char(c);
  else
    eval::funcall(gray::_sym_stream_unread_char, stream, clasp_make_character(c));
}

claspCharacter stream_peek_char(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->peek_char();

  T_sp out = eval::funcall(gray::_sym_stream_peek_char, stream);
  if (out == kw::_sym_eof)
    return EOF;
  return clasp_as_claspCharacter(gc::As<Character_sp>(out));
}

cl_index stream_read_vector(T_sp stream, T_sp data, cl_index start, cl_index end) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->read_vector(data, start, end);

  T_sp fn = eval::funcall(gray::_sym_stream_read_sequence, stream, data, clasp_make_fixnum(start), clasp_make_fixnum(end));
  if (fn.fixnump()) {
    return fn.unsafe_fixnum();
  }
  SIMPLE_ERROR("gray:stream-read-sequence returned a non-integer {}", _rep_(fn));
}

cl_index stream_write_vector(T_sp stream, T_sp data, cl_index start, cl_index end) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->write_vector(data, start, end);

  eval::funcall(gray::_sym_stream_write_sequence, stream, data, clasp_make_fixnum(start), clasp_make_fixnum(end));
  if (start >= end)
    return start;
  return end;
}

ListenResult stream_listen(T_sp stream) {
  return stream.isA<AnsiStream_O>()
             ? stream.as_unsafe<AnsiStream_O>()->listen()
             : ((T_sp(eval::funcall(gray::_sym_stream_listen, stream))).nilp() ? listen_result_no_char : listen_result_available);
}

void stream_clear_input(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->clear_input();
  else
    eval::funcall(gray::_sym_stream_clear_input, stream);
}

void stream_clear_output(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->clear_output();
  else
    eval::funcall(gray::_sym_stream_clear_output, stream);
}

void stream_finish_output(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->finish_output();
  else
    eval::funcall(gray::_sym_stream_finish_output, stream);
}

void stream_force_output(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    stream.as_unsafe<AnsiStream_O>()->force_output();
  else
    eval::funcall(gray::_sym_stream_force_output, stream);
}

bool stream_open_p(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->open_p()
                                    : T_sp(eval::funcall(gray::_sym_open_stream_p, stream)).notnilp();
}

bool stream_p(T_sp stream) {
  return stream.isA<Stream_O>() || (gray::_sym_streamp->fboundp() && T_sp(eval::funcall(gray::_sym_streamp, stream)).notnilp());
}

bool stream_input_p(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->input_p()
                                    : T_sp(eval::funcall(gray::_sym_input_stream_p, stream)).notnilp();
}

bool stream_output_p(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->output_p()
                                    : T_sp(eval::funcall(gray::_sym_output_stream_p, stream)).notnilp();
}

bool stream_interactive_p(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->interactive_p()
                                    : T_sp(eval::funcall(gray::_sym_stream_interactive_p, stream)).notnilp();
}

T_sp stream_element_type(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->element_type()
                                    : eval::funcall(gray::_sym_stream_element_type, stream);
}

T_sp stream_set_element_type(T_sp stream, T_sp type) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->set_element_type(type) : _lisp->_true();
}

T_sp stream_external_format(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->external_format() : (T_sp)kw::_sym_default;
}

T_sp stream_set_external_format(T_sp stream, T_sp format) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->set_external_format(format) : (T_sp)kw::_sym_default;
}

T_sp stream_length(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->length()
                                    : eval::funcall(gray::_sym_stream_file_length, stream);
}

T_sp stream_position(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->position()
                                    : eval::funcall(gray::_sym_stream_file_position, stream);
}

T_sp stream_set_position(T_sp stream, T_sp pos) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->set_position(pos)
                                    : eval::funcall(gray::_sym_stream_file_position, stream, pos);
}

T_sp stream_string_length(T_sp stream, T_sp string) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->string_length(string) : nil<T_O>();
}

int stream_column(T_sp stream) {
  if (stream.isA<AnsiStream_O>())
    return stream.as_unsafe<AnsiStream_O>()->column();

  T_sp col = eval::funcall(gray::_sym_stream_line_column, stream);
  // negative columns represent NIL
  return col.nilp() ? -1 : clasp_to_integral<int>(clasp_floor1(gc::As<Real_sp>(col)));
}

int stream_set_column(T_sp stream, int column) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->set_column(column) : column;
}

int stream_input_handle(T_sp stream) { return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->input_handle() : -1; }

int stream_output_handle(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->output_handle() : -1;
}

T_sp stream_close(T_sp stream, T_sp abort) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->close(abort)
                                    : eval::funcall(gray::_sym_close, stream, kw::_sym_abort, abort);
}

T_sp stream_pathname(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->pathname() : eval::funcall(gray::_sym_pathname, stream);
}

T_sp stream_truename(T_sp stream) {
  return stream.isA<AnsiStream_O>() ? stream.as_unsafe<AnsiStream_O>()->truename() : eval::funcall(gray::_sym_truename, stream);
}

}; // namespace core
