#pragma once
/*
    File: designators.h
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

#include <clasp/core/object.h>
#include <clasp/core/corePackage.fwd.h>
#include <clasp/core/fileSystem.h>
#include <clasp/core/pathname.h>

namespace core {

namespace coerce {

/* From CLHS:
           function designator - a designator for a function; that is, an object that denotes a
           function and that is one of: a symbol (denoting the function named by that symbol in
           the global environment), or a function (denoting itself). The consequences are
           undefined if a symbol is used as a function designator but it does not have a global
           definition as a function, or it has a global definition as a macro or a special form.
           See also extended function designator.
        */
extern Function_sp functionDesignator(T_sp obj);
extern Function_sp closureDesignator(T_sp obj);
// Coerce a designator that's about to be called.
// This lets us save an fboundp check.
extern Function_sp calledFunctionDesignator(T_sp obj);

#if 0
/*! Return a Path by interpreting a pathname designator */
extern Path_sp pathDesignator(T_sp obj);
#endif

/*! Return a Package by interpreting a package designator */
extern Package_sp packageDesignator(T_sp obj);
/*! Return a Package by interpreting a package designator, or nil if not found */
extern T_sp packageDesignatorNoError(T_sp obj);

/*! Return the name of a Package by interpreting a package or a string as a name */
extern string packageNameDesignator(T_sp obj);

/*! Return a List of packages by interpreting as a list of package designators */
extern List_sp listOfPackageDesignators(T_sp obj);

/*! Coerce the obj to a simple-string */
SimpleString_sp simple_string(T_sp obj);

extern String_sp stringDesignator(T_sp obj);

/*! Return a List of strings by interpreting the
          object as a list of string designators */
extern List_sp listOfStringDesignators(T_sp obj);

/*! Return a List of symbols by interpreting a designator for a list of symbols */
extern List_sp listOfSymbols(T_sp obj);

/*! Convert an Object input stream designator (as described by CLHS) into a Stream */
T_sp inputStreamDesignator(T_sp obj);

/*! Convert an Object output stream designator (as described by CLHS) into a Stream */
T_sp outputStreamDesignator(T_sp obj);

#if 0
 cl_index coerceToEndInRangeOrError(T_sp tend, cl_index start, cl_index end);

 void inBoundsOrError(cl_index index, cl_index start, cl_index end);
 void inBoundsBelowEndOrError(cl_index index, cl_index start, cl_index end);
#endif

T_sp coerce_to_base_string(T_sp str);

}; // namespace coerce

}; // namespace core
