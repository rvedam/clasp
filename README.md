Clasp
===============
Clasp is a Common Lisp implementation that interoperates with C++ and uses LLVM for just-in-time (JIT) compilation to native code.

See http://drmeister.wordpress.com/2014/09/18/announcing-clasp/ for the announcement.

Clasp is not yet a full ANSI compliant Common Lisp - if you find differences between Clasp and the Common Lisp standard they are considered bugs in Clasp and please feel free to report them.

Libraries that clasp depends on can be obtained using the repository: externals-clasp
https://github.com/drmeister/externals-clasp.git
You can build externals-clasp or you can configure your environment by hand.

**INSTALLATION**

Clasp has been compiled on OS X 10.9.5 using Xcode 6.0.1

Clasp has been compiled on recent (post 2013) versions of Ubuntu Linux

To build Clasp from within the top level directory do the following.

1) Copy local.config.darwin or local.config.linux to local.config

2) Edit local.config and configure it for your system

3) Type: _make_   to build mps and boehm versions of Clasp 

or type: _make-boehm_   to make the boehm version of Clasp

or type: _make-mps_     to make the MPS version of Clasp

4) Install the directory in $PREFIX/MacOS or $PREFIX/bin (from local.config) in your path<p>
   then type: clasp_mps_o     to start the Lisp REPL of the MPS version of Clasp
   or type:   clasp_boehm_o   to start the Lisp REPL of the Boehm version of Clasp

5) Type: (print "Hello world")  in the REPL and away you go (more documentation to follow)


If you want to install the libraries separately they are:<p>
Contact me for more info - I can add more details to what is below.<p>
Boost build v2<p>
boost libraries ver 1.55<p>
Boehm 7.2<p>
LLVM/clang 3.5<p>
ecl ver 12<p>
gmp-5.0.5<p>
expat-2.0.1<p>
zlib-1.2.8<p>
readline-6.2<p>
                                                                                                        
