/*
    File: wrappedPointer.cc
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
#include <clasp/core/wrappedPointer.h>
#include <clasp/core/lispStream.h>
#include <clasp/core/wrappers.h>

namespace core {

CL_LAMBDA(arg);
CL_DECLARE();
CL_DOCSTRING(R"dx(pointerRelease)dx");
DOCGROUP(clasp);
CL_DEFUN Pointer_sp core__pointer_release(T_sp ptr) {
  if (ptr.nilp()) {
    return nil<Pointer_O>();
  };
  if (WrappedPointer_sp wp = ptr.asOrNull<WrappedPointer_O>()) {
    return Pointer_O::create(wp->pointerRelease());
  }
  SIMPLE_ERROR("Could not release pointer for {}", _rep_(ptr));
}

CL_LAMBDA(arg);
CL_DECLARE();
CL_DOCSTRING(R"dx(pointerDelete)dx");
DOCGROUP(clasp);
CL_DEFUN void core__pointer_delete(T_sp ptr) {
  if (ptr.nilp()) {
    return;
  };
  if (WrappedPointer_sp wp = ptr.asOrNull<WrappedPointer_O>()) {
    wp->pointerDelete();
    return;
  }
  SIMPLE_ERROR("Could not release pointer for {}", _rep_(ptr));
}

T_sp WrappedPointer_O::_instanceClassSet(Instance_sp cl) {
  this->Class_ = cl;
  this->ShiftedStamp_ = cl->CLASS_stamp_for_instances();
  if (!gctools::Header_s::StampWtagMtag::is_wrapped_shifted_stamp(this->ShiftedStamp_)) {
    printf("%s:%d When setting the class of an instance to class: %s - the ShiftedStamp_ %lu is not a wrapped_shifted_stamp\n",
           __FILE__, __LINE__, _rep_(cl).c_str(), this->ShiftedStamp_);
  }
  ASSERT(gctools::Header_s::StampWtagMtag::is_wrapped_shifted_stamp(this->ShiftedStamp_));
  return this->asSmartPtr();
}

void WrappedPointer_O::_setInstanceClassUsingSymbol(Symbol_sp classSymbol) {
  Instance_sp cl = gc::As<Instance_sp>(cl__find_class(classSymbol));
  this->_instanceClassSet(cl);
}

std::string WrappedPointer_O::__repr__() const {
  stringstream ss;
  ss << "#<wrapped-pointer :ptr ";
  ss << (void*)this->mostDerivedPointer();
  ss << " @";
  ss << (void*)this;
  ss << ">";
  return ss.str();
}

bool WrappedPointer_O::eql_(T_sp obj) const {
  if (WrappedPointer_sp wo = obj.asOrNull<WrappedPointer_O>()) {
    return (wo->mostDerivedPointer() == this->mostDerivedPointer());
  }
  return false;
}

Pointer_sp WrappedPointer_O::address() const {
  void* addr = this->mostDerivedPointer();
  return Pointer_O::create(addr);
}

CL_LAMBDA(arg);
CL_DECLARE();
CL_DOCSTRING(R"dx(pointerAddress)dx");
DOCGROUP(clasp);
CL_DEFUN T_sp core__pointer_address(T_sp ptr) {
  if (ptr.nilp()) {
    return nil<Pointer_O>();
  };
  if (WrappedPointer_sp wp = ptr.asOrNull<WrappedPointer_O>()) {
    return wp->address();
  }
  SIMPLE_ERROR("Could not get address of pointer for {}", _rep_(ptr));
};

DOCGROUP(clasp);
CL_DEFUN void core__verify_wrapped_pointer_layout(size_t stamp_offset) {
  size_t cxx_stamp_offset = offsetof(WrappedPointer_O, ShiftedStamp_);
  if (stamp_offset != cxx_stamp_offset)
    SIMPLE_ERROR("stamp_offset {} does not match cxx_stamp_offset {}", stamp_offset, cxx_stamp_offset);
}

}; // namespace core

#define READ_WRAPPED_STAMP
#include <clasp/llvmo/read-stamp.cc>
#undef READ_WRAPPED_STAMP

namespace core {

CL_DEFUN T_sp core__wrapped_stamp(T_sp obj) {
  General_O* client_ptr = gctools::untag_general<General_O*>((General_O*)obj.raw_());
  uintptr_t stamp = (uintptr_t)(llvmo::template_read_wrapped_stamp(client_ptr));
  //  core::clasp_write_string(fmt::format("{}:{}:{} stamp = {}u\n", __FILE__, __LINE__, __FUNCTION__, stamp ));
  T_sp result((gctools::Tagged)stamp);
  return result;
}

}; // namespace core
