/*
    File: grayPackage.cc
*/

/*
Copyright (c) 2014, Christian E. Schafmeister

CLASP is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

See directory 'clasp/licenses' for full details.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
/* -^- */

#include <clasp/core/foundation.h>
#include <clasp/core/object.h>
#include <clasp/core/lisp.h>
#include <clasp/core/symbol.h>
#include <clasp/core/grayPackage.h>
#include <clasp/core/multipleValues.h>
#include <clasp/core/package.h>

namespace gray {

SYMBOL_EXPORT_SC_(GrayPkg, stream_advance_to_column);
SYMBOL_EXPORT_SC_(GrayPkg, stream_clear_input);
SYMBOL_EXPORT_SC_(GrayPkg, stream_clear_output);
SYMBOL_EXPORT_SC_(GrayPkg, stream_file_descriptor);
SYMBOL_EXPORT_SC_(GrayPkg, stream_file_length);
SYMBOL_EXPORT_SC_(GrayPkg, stream_file_position);
SYMBOL_EXPORT_SC_(GrayPkg, stream_file_string_length);
SYMBOL_EXPORT_SC_(GrayPkg, stream_finish_output);
SYMBOL_EXPORT_SC_(GrayPkg, stream_force_output);
SYMBOL_EXPORT_SC_(GrayPkg, stream_fresh_line);
SYMBOL_EXPORT_SC_(GrayPkg, stream_input_column);
SYMBOL_EXPORT_SC_(GrayPkg, stream_input_line);
SYMBOL_EXPORT_SC_(GrayPkg, stream_interactive_p);
SYMBOL_EXPORT_SC_(GrayPkg, stream_line_column);
SYMBOL_EXPORT_SC_(GrayPkg, stream_line_length);
SYMBOL_EXPORT_SC_(GrayPkg, stream_line_number);
SYMBOL_EXPORT_SC_(GrayPkg, stream_listen);
SYMBOL_EXPORT_SC_(GrayPkg, stream_peek_char);
SYMBOL_EXPORT_SC_(GrayPkg, stream_read_byte);
SYMBOL_EXPORT_SC_(GrayPkg, stream_read_char);
SYMBOL_EXPORT_SC_(GrayPkg, stream_read_char_no_hang);
SYMBOL_EXPORT_SC_(GrayPkg, stream_read_line);
SYMBOL_EXPORT_SC_(GrayPkg, stream_read_sequence);
SYMBOL_EXPORT_SC_(GrayPkg, stream_start_line_p);
SYMBOL_EXPORT_SC_(GrayPkg, stream_terpri);
SYMBOL_EXPORT_SC_(GrayPkg, stream_unread_char);
SYMBOL_EXPORT_SC_(GrayPkg, stream_write_byte);
SYMBOL_EXPORT_SC_(GrayPkg, stream_write_char);
SYMBOL_EXPORT_SC_(GrayPkg, stream_write_sequence);
SYMBOL_EXPORT_SC_(GrayPkg, stream_write_string);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, close);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, input_stream_p);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, open_stream_p);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, output_stream_p);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, pathname);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, stream_element_type);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, stream_external_format);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, streamp);
SYMBOL_SHADOW_EXPORT_SC_(GrayPkg, truename);

void initialize_grayPackage() {
  list<string> lnicknames;
  list<string> luse = {"COMMON-LISP"};
  _lisp->makePackage("GRAY", lnicknames, luse);
  // We don't have to create the GRAY symbols here - it's done in bootStrapCoreSymbolMap
}
}; // namespace gray
