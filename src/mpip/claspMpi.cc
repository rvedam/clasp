/*
    File: claspMpi.cc
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
#define DEBUG_LEVEL_FULL

#include <clasp/core/foundation.h>
#ifdef USE_MPI
#include <boost/mpi.hpp>
#endif
#include <clasp/core/object.h>
#include <clasp/core/lisp.h>
#include <clasp/core/cons.h>
#include <clasp/core/evaluator.h>
#include <clasp/core/lispStream.h>
#include <clasp/mpip/claspMpi.h>
#include <clasp/core/wrappers.h>

/*
__BEGIN_DOC( mpi, chapter, MPI Message Passing core::Interface )

Cando contains a subset of MPI functionality to allow {\CANDOSCRIPT} programs to be written that run in parallel on multiple
computers.

Complex {\CANDOSCRIPT} objects are sent back and forth between processes using {\CANDOSCRIPT}s powerful XML serialization code.

In this example code worker processes create Rectangle objects and send them back to a manager process which prints them on the
screen.

You can run it by saving it in the file mpi.csc and running: mpirun -np 5 candoMpi mpi.csc


__END_DOC
*/

namespace mpip {

SYMBOL_EXPORT_SC_(MpiPkg, MpiTermConverter);

static bool _MpiInitialized = false;
static bool _MpiWorldInitialized = false;

#ifdef USE_MPI
static boost::mpi::environment* _MpiEnvironment;
#endif

/*
  __BEGIN_DOC( mpi.commands, section, MPI Functions)

  A list of MPI functions.
  __END_DOC
*/

/*
  __BEGIN_DOC( mpi.commands.mpiEnabled, subsection, mpiEnabled)
  \scriptCmdRet{mpiEnabled}{}{core::Bool::}

  Return true if MPI is enabled and false if it is not. This function is available in all implementations of {\CANDOSCRIPT}.
  __END_DOC
*/

bool Mpi_O::mpiEnabled() {
  _G();
#ifdef USE_MPI
  return true;
#else
  return false;
#endif
}

/*
  __BEGIN_DOC( mpi.commands.mpiSize, subsection, mpiSize)
  \scriptCmdRet{mpiSize}{}{core::Int::}

  Returns the number of processes available.  This function is available in all implementations of {\CANDOSCRIPT} but if MPI is not
  enabled it returns 1.
  __END_DOC
*/
int Mpi_O::mpiSize() {
  _G();
#ifdef USE_MPI
  return mpiCommWorld()->Get_size();
#else
  return 1;
#endif
}

/*
  __BEGIN_DOC( mpi.commands.mpiRank, subsection, mpiRank)
  \scriptCmdRet{mpiRank}{}{core::Int::}

  Returns the rank of the current processes. The rank is a number from 0 to ([mpiSize] - 1). This function is available in all
  implementations of {\CANDOSCRIPT} but if MPI is not enabled it returns 0.
  __END_DOC
*/
int Mpi_O::mpiRank() {
#ifdef USE_MPI
  return mpiCommWorld()->Get_rank();
#else
  return 0;
#endif
}

void Mpi_O::Init(int& argc, char**& argv, bool& mpiEnabled, int& rank, int& msize) {
  mpiEnabled = false;
  rank = -1;
  msize = -1;
// Do not touch the debug log in this function
#ifdef USE_MPI
  _MpiEnvironment = new boost::mpi::environment(argc, argv);
  HARD_ASSERTF(_MpiEnvironment, "Could not create MPI environment although mpi should be enabled");
  _MpiInitialized = _MpiEnvironment->initialized();
  if (_MpiInitialized) {
    boost::mpi::communicator world;
    mpiEnabled = true;
    rank = world.rank();
    msize = world.size();
  } else {
    printf("%s %d Could not initialize mpi - exiting\n", __FILE__, __LINE__);
    exit(1);
  }
#else
  _MpiInitialized = false;
#endif
}

/*
  __BEGIN_DOC( mpi.commands.mpiCommWorld, subsection, mpiCommWorld)
  \scriptCmdRet{mpiCommWorld}{}{Mpi::}

  Returns the MPI object that encompases the entire group of processes.
  __END_DOC
*/
Mpi_sp Mpi_O::mpiCommWorld() {
  _G();
  if (!_MpiWorldInitialized) {
    _MpiWorldInitialized = true;
    LOG("_MpiWorld creating"); // vp0(( "_MpiWorld creating" ));
    _lisp->_Roots._MpiWorld = Mpi_O::create();
  }
  return _lisp->_Roots._MpiWorld;
}

void Mpi_O::Finalize() {
  _G();
#ifdef USE_MPI
  delete (_MpiEnvironment);
#endif
}

/*
  __BEGIN_DOC(mpi.MpiObject,section,MPI Object methods)

  core::Methods that can be sent to MPI objects.
  __END_DOC
*/

void Mpi_O::initialize() {
  this->Base::initialize();
  this->_Source = 0;
  this->_Tag = 0;
}

CL_DEFMETHOD int Mpi_O::Get_size() {
  _G();
#ifdef USE_MPI
  int size;
  size = this->_Communicator.size();
  return size;
#else
  return 1;
#endif
}

CL_DEFMETHOD int Mpi_O::Get_rank() {
#ifdef USE_MPI
  int rank;
  rank = this->_Communicator.rank();
  return rank;
#else
  return 0;
#endif
}

SYMBOL_EXPORT_SC_(MpiPkg, STARencode_object_hookSTAR);
SYMBOL_EXPORT_SC_(MpiPkg, STARdecode_object_hookSTAR);

// Object_sp obj, int dest, int tag )
/*
  __BEGIN_DOC( mpi.MpiObject.Send, subsection, Send)
  \scriptcore::Method{mpi}{Send}{Object::data core::Int::dest core::Int::tag}

  Sends the \sa{Object::data} to the process \sa{dest} with the tag \sa{tag}. The data can be any Cando-Script object - it is
  archived into XML format and then sent to the process \sa{dest} and then dearchived back into a Cando-Script object on the other
  side.
  __END_DOC
*/
CL_DEFMETHOD core::T_sp Mpi_O::prim_Send(int dest, int tag, core::T_sp obj) {
#ifdef USE_MPI
  core::T_sp msg = core::eval::funcall(_sym_STARencode_object_hookSTAR->symbolValue(), obj);
  core::SimpleBaseString_sp sbs = gc::As<core::SimpleBaseString_sp>(msg);
  LOG("About to call MPI_Send\n%s\n", sbs->get_std_string());
  this->_Communicator.send(dest, tag, sbs->get_std_string());
#endif
  return nil<core::T_O>();
}

/*
  __BEGIN_DOC( mpi.Mpicore::T.Recv, subsection, Recv)
  \scriptMethodRet{mpi}{Recv}{core::Int::dest core::Int::tag}{core::T::data}

  Blocks and waits for data from the process \sa{dest} with the requested tag \sa{tag}. You can provide the value MPI::ANY\_SOURCE
  if you want to receive data from any source and MPI::ANY\_TAG if you want any tag. You can use the \scmd{GetSource} and
  \scmd{GetTag} to query the source and tag that the sender sent. The data that is received is returned in \sa{core::T::data}.
  __END_DOC
*/
CL_DEFMETHOD core::T_mv Mpi_O::prim_Recv(int source, int tag) {
#ifdef USE_MPI
  LOG("About to call MPI_Probe"); // vp0(("About to call MPI_Probe"));
  boost::mpi::status stat = this->_Communicator.probe(source, tag);
  this->_Source = stat.source();
  this->_Tag = stat.tag();
  LOG("Probe command returned source %d", this->_Source); // vp0(("Probe command returned source %d", this->_Source ));
  string buffer;
  this->_Communicator.recv(source, tag, buffer);
  core::SimpleBaseString_sp sis = core::SimpleBaseString_O::make(buffer);
  core::T_sp obj = core::eval::funcall(_sym_STARdecode_object_hookSTAR->symbolValue(), sis);
  return Values(obj, core::make_fixnum(this->_Source), core::make_fixnum(this->_Tag));
#else
  return nil<core::T_O>();
#endif
}

/*
  __BEGIN_DOC( mpi.Mpicore::T.GetSource, subsection, GetSource)
  \scriptMethodRet{mpi}{GetSource}{}{core::Int::source}

  Returns the source for the most recent Recv command.
  __END_DOC
*/
CL_DEFMETHOD int Mpi_O::Get_source() { return this->_Source; }

/*
  __BEGIN_DOC( mpi.Mpicore::T.GetTag, subsection, GetTag)
  \scriptMethodRet{mpi}{GetTag}{}{core::Int::tag}

  Returns the tag for the most recent Recv command.
  __END_DOC
*/
CL_DEFMETHOD int Mpi_O::Get_tag() { return this->_Tag; }

void Mpi_O::initializeGlobals(core::LispPtr lisp) {
#ifdef USE_MPI
  SYMBOL_EXPORT_SC_(MpiPkg, _PLUS_anySource_PLUS_);
  SYMBOL_EXPORT_SC_(MpiPkg, _PLUS_anyTag_PLUS_);
  core::Symbol_sp anySource = _lisp->internWithPackageName(MpiPkg, "ANY_SOURCE");
  mpip::_sym__PLUS_anySource_PLUS_->defconstant(core::make_fixnum(boost::mpi::any_source));
  mpip::_sym__PLUS_anyTag_PLUS_->defconstant(core::make_fixnum(boost::mpi::any_tag));
  SYMBOL_EXPORT_SC_(MpiPkg, STARworldSTAR);
  Mpi_sp world = Mpi_O::mpiCommWorld();
  _sym_STARworldSTAR->defparameter(world);
#endif
}
#if 0
void Mpi_O::exposeCando(core::LispPtr lisp) {
  _G();
  LOG("Exposing Mpi_O"); // vp0(("Exposing Mpi_O"));
  core::af_def(MpiPkg, "world", &Mpi_O::mpiCommWorld);
  core::class_<Mpi_O>()
      .def("size", &Mpi_O::Get_size)
      .def("rank", &Mpi_O::Get_rank)
      .def("source", &Mpi_O::Get_source)
      .def("tag", &Mpi_O::Get_tag)
      .def("send", &Mpi_O::prim_Send)
      .def("recv", &Mpi_O::prim_Recv);
}

void Mpi_O::exposePython(core::LispPtr lisp) {
#ifdef USEBOOSTPYTHON //[
  boost::python::class_<Mpi_O,
                        boost::shared_ptr<Mpi_O>,
                        boost::python::bases<core::T_O>,
                        boost::noncopyable>("Mpi_O", boost::python::no_init);
#endif
}
#endif
}; // namespace mpip
