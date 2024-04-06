(in-package #:clasp-cleavir)

;;; A "primop" is something that can be "called" like a function (all its
;;; arguments are evaluated) but which is specially translated by the compiler.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; CORE:PRIMOP special operator
;;;
;;; This allows primops to be used directly in source code. Use with caution.
;;;

(defmethod cst-to-ast:convert-special ((symbol (eql 'core::primop)) cst env
                                       (system clasp-cleavir:clasp))
  (unless (cst:proper-list-p cst)
    (error 'cleavir-cst-to-ast:form-must-be-proper-list :cst cst))
  (let* ((name (cst:raw (cst:second cst)))
         (op (cleavir-primop-info:info name))
         (nargs (cleavir-primop-info:ninputs op)))
    (let ((count (- (length (cst:raw cst)) 2)))
      (unless (= count nargs) ; 2 for PRIMOP and the name
        (error 'cst-to-ast:incorrect-number-of-arguments-error
               :cst cst :expected-min nargs :expected-max nargs
               :observed count)))
    (make-instance 'cleavir-ast:primop-ast
      :info op
      :argument-asts (cst-to-ast::convert-sequence
                      (cst:rest (cst:rest cst))
                      env system)
      :origin cst)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; Primop definition machinery
;;;

;;; Called by translate-simple-instruction. Return value irrelevant.
(defgeneric translate-primop (opname instruction))

;;; Hash table from primop infos to rtype info.
;;; An rtype info is just a list (return-rtype argument-rtypes...)
;;; For almost all primops, we just put in an entry in the table,
;;; but for some compound rtypes we use %primop-rtype-info to compute.
;;; See bir-to-bmir for more information about rtypes.
(defvar *primop-rtypes* (make-hash-table :test #'equal))

(defgeneric %primop-rtype-info (name primop-info))

;;; Default method: Assume all :object.
(defmethod %primop-rtype-info (name primop-info)
  (declare (ignore name))
  (list* '(:object)
         (make-list (cleavir-primop-info:ninputs primop-info)
                    :initial-element :object)))

(defun primop-rtype-info (primop-info)
  (let* ((name (cleavir-primop-info:name primop-info))
         (aname
           (list* name (cleavir-primop-info:arguments primop-info))))
    (or (gethash aname *primop-rtypes*)
        (setf (gethash aname *primop-rtypes*)
              (%primop-rtype-info name primop-info)))))

;;; Define a primop that returns values.
;;; param-info is either (return-rtype param-rtypes...) or an integer; the
;;; latter is short for taking that many :objects and returning (:object).
;;; For example, (defvprimop foo 2)
;;;              = (defvprimop foo ((:object) :object :object))
;;; The BODY is used as a translate-primop method, where the call instruction
;;; is available bound to INSTPARAM.
;;; The NAME can be a symbol or a list (SYMBOL ...) where ... are options,
;;; sort of like defstruct. So far the only option is :flags.
(defmacro defvprimop (name param-info (instparam) &body body)
  (let ((name (if (consp name) (first name) name))
        (options (if (consp name) (rest name) nil))
        (param-info (if (integerp param-info)
                        (list* '(:object) (make-list param-info
                                                     :initial-element :object))
                        param-info))
        (nsym (gensym "NAME")))
    (destructuring-bind (&key flags) options
      `(progn
         (cleavir-primop-info:defprimop ,name ,(length (rest param-info))
           :value ,@flags)
         (setf (gethash '(,name) *primop-rtypes*) '(,@param-info))
         (defmethod translate-primop ((,nsym (eql ',name)) ,instparam)
           (out (progn ,@body) (first (bir:outputs ,instparam))))
         ',name))))

;;; Like defvprimop for the case where the body is just an intrinsic.
(defmacro defvprimop-intrinsic (name param-info intrinsic)
  ;; TODO: Assert argument types? Or maybe that should be done at a lower level
  ;; in irc-intrinsic-etc.
  `(defvprimop ,name ,param-info (inst)
     (%intrinsic-invoke-if-landing-pad-or-call
      ,intrinsic (mapcar #'in (bir:inputs inst)))))

;;; Define a primop called for effect.
;;; Here param-info is parameters only.
(defmacro defeprimop (name param-info (instparam) &body body)
  (let ((name (if (consp name) (first name) name))
        (options (if (consp name) (rest name) nil))
        (param-info
          (list* () (if (integerp param-info)
                        (make-list param-info :initial-element :object)
                        param-info)))
        (nsym (gensym "NAME")))
    (destructuring-bind (&key flags) options
      `(progn
         (cleavir-primop-info:defprimop ,name ,(length (rest param-info))
           :effect ,@flags)
         (setf (gethash '(,name) *primop-rtypes*) '(,@param-info))
         (defmethod translate-primop ((,nsym (eql ',name)) ,instparam)
           ,@body
           (when (bir:outputs ,instparam)
             (out nil (first (bir:outputs ,instparam)))))
         ',name))))

;;; Define a primop used as a conditional test.
;;; Here param-info is parameters only.
(defmacro deftprimop (name param-info (instparam) &body body)
  `(defvprimop ,name ((:boolean) ,@(if (integerp param-info)
                                       (make-list param-info :initial-element :object)
                                       param-info))
     (,instparam)
     ,@body))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; Particular primops
;;;

(macrolet ((def-float-compare (sfname dfname op reversep)
             `(progn
                (deftprimop ,sfname (:single-float :single-float)
                  (inst)
                  (assert (= (length (bir:inputs inst)) 2))
                  (let ((,(if reversep 'i2 'i1)
                          (in (first (bir:inputs inst))))
                        (,(if reversep 'i1 'i2)
                          (in (second (bir:inputs inst)))))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i1)
                                                 cmp:%float%))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i2)
                                                 cmp:%float%))
                    (,op i1 i2)))
                (deftprimop ,dfname (:double-float :double-float)
                  (inst)
                  (assert (= (length (bir:inputs inst)) 2))
                  (let ((,(if reversep 'i2 'i1)
                          (in (first (bir:inputs inst))))
                        (,(if reversep 'i1 'i2)
                          (in (second (bir:inputs inst)))))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i1)
                                                 cmp:%double%))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i2)
                                                 cmp:%double%))
                    (,op i1 i2))))))
  (def-float-compare core::two-arg-sf-=  core::two-arg-df-=  %fcmp-oeq nil)
  (def-float-compare core::two-arg-sf-<  core::two-arg-df-<  %fcmp-olt nil)
  (def-float-compare core::two-arg-sf-<= core::two-arg-df-<= %fcmp-ole nil)
  (def-float-compare core::two-arg-sf->  core::two-arg-df->  %fcmp-olt   t)
  (def-float-compare core::two-arg-sf->= core::two-arg-df->= %fcmp-ole   t))

(macrolet ((def-float-unop (sfname sfintrinsic dfname dfintrinsic)
             `(progn
                ;; NOTE: marking these flushable might change fp exception
                ;; behavior - do we care? not sure.
                (defvprimop-intrinsic (,sfname :flags (:flushable))
                    ((:single-float) :single-float)
                  ,sfintrinsic)
                (defvprimop-intrinsic (,dfname :flags (:flushable))
                    ((:double-float) :double-float)
                  ,dfintrinsic))))
  (def-float-unop core::sf-abs   "llvm.fabs.f32" core::df-abs   "llvm.fabs.f64")
  (def-float-unop core::sf-sqrt  "llvm.sqrt.f32" core::df-sqrt  "llvm.sqrt.f64")
  (def-float-unop core::sf-exp   "llvm.exp.f32"  core::df-exp   "llvm.exp.f64")
  (def-float-unop core::sf-log   "llvm.log.f32"  core::df-log   "llvm.log.f64")
  (def-float-unop core::sf-cos   "llvm.cos.f32"  core::df-cos   "llvm.cos.f64")
  (def-float-unop core::sf-sin   "llvm.sin.f32"  core::df-sin   "llvm.sin.f64")
  (def-float-unop core::sf-tan   "tanf"          core::df-tan   "tan")
  (def-float-unop core::sf-acos  "acosf"         core::df-acos  "acos")
  (def-float-unop core::sf-asin  "asinf"         core::df-asin  "asin")
  (def-float-unop core::sf-cosh  "coshf"         core::df-cosh  "cosh")
  (def-float-unop core::sf-sinh  "sinhf"         core::df-sinh  "sinh")
  (def-float-unop core::sf-tanh  "tanhf"         core::df-tanh  "tanh")
  (def-float-unop core::sf-acosh "acoshf"        core::df-acosh "acosh")
  (def-float-unop core::sf-asinh "asinhf"        core::df-asinh "asinh")
  (def-float-unop core::sf-atanh "atanhf"        core::df-atanh "atanh"))

(macrolet ((def-float-binop-op (sfname dfname ircop)
             `(progn
                (defvprimop (,sfname :flags (:flushable))
                    ((:single-float) :single-float :single-float)
                  (inst)
                  (assert (= 2 (length (bir:inputs inst))))
                  (let ((i1 (in (first (bir:inputs inst))))
                        (i2 (in (second (bir:inputs inst)))))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i1)
                                                 cmp:%float%))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i2)
                                                 cmp:%float%))
                    (,ircop i1 i2)))
                (defvprimop (,dfname :flags (:flushable))
                    ((:double-float) :double-float :double-float)
                  (inst)
                  (assert (= 2 (length (bir:inputs inst))))
                  (let ((i1 (in (first (bir:inputs inst))))
                        (i2 (in (second (bir:inputs inst)))))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i1)
                                                 cmp:%double%))
                    (assert (llvm-sys:type-equal (llvm-sys:get-type i2)
                                                 cmp:%double%))
                    (,ircop i1 i2)))))
           (def-float-binop-i (sfname sfintrinsic dfname dfintrinsic)
             `(progn
                (defvprimop-intrinsic ,sfname
                    ((:single-float) :single-float :single-float)
                  ,sfintrinsic)
                (defvprimop-intrinsic ,dfname
                    ((:double-float) :double-float :double-float)
                  ,dfintrinsic))))
  (def-float-binop-op core::two-arg-sf-+ core::two-arg-df-+ %fadd)
  (def-float-binop-op core::two-arg-sf-- core::two-arg-df-- %fsub)
  (def-float-binop-op core::two-arg-sf-* core::two-arg-df-* %fmul)
  (def-float-binop-op core::two-arg-sf-/ core::two-arg-df-/ %fdiv)
  (def-float-binop-i core::sf-expt "llvm.pow.f32" core::df-expt "llvm.pow.f64"))

(defvprimop (core::sf-ftruncate :flags (:flushable))
    ((:single-float :single-float) :single-float :single-float)
  (inst)
  (assert (= 2 (length (bir:inputs inst))))
  (let ((i1 (in (first (bir:inputs inst))))
        (i2 (in (second (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type i1) cmp:%float%))
    (assert (llvm-sys:type-equal (llvm-sys:get-type i2) cmp:%float%))
    ;; I think this is the best instruction sequence, but I am not sure.
    (list (%intrinsic-call "llvm.trunc.f32" (list (%fdiv i1 i2)))
          (%frem i1 i2))))
(defvprimop (core::df-ftruncate :flags (:flushable))
    ((:double-float :double-float) :double-float :double-float)
  (inst)
  (assert (= 2 (length (bir:inputs inst))))
  (let ((i1 (in (first (bir:inputs inst))))
        (i2 (in (second (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type i1) cmp:%double%))
    (assert (llvm-sys:type-equal (llvm-sys:get-type i2) cmp:%double%))
    (list (%intrinsic-call "llvm.trunc.f64" (list (%fdiv i1 i2)))
          (%frem i1 i2))))

(defvprimop (core::sf-negate :flags (:flushable))
    ((:single-float) :single-float) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (let ((arg (in (first (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type arg) cmp:%float%))
    (%fneg arg)))
(defvprimop (core::df-negate :flags (:flushable))
    ((:double-float) :double-float) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (let ((arg (in (first (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type arg) cmp:%double%))
    (%fneg arg)))

(defvprimop (core::single-to-double :flags (:flushable))
    ((:double-float) :single-float) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (let ((arg (in (first (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type arg) cmp:%float%))
    (%fpext arg cmp:%double%)))
(defvprimop (core::double-to-single :flags (:flushable))
    ((:single-float) :double-float) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (let ((arg (in (first (bir:inputs inst)))))
    (assert (llvm-sys:type-equal (llvm-sys:get-type arg) cmp:%double%))
    (%fptrunc arg cmp:%float%)))

(defvprimop (core::fixnum-to-single :flags (:flushable))
    ((:single-float) :utfixnum) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (%sitofp (in (first (bir:inputs inst))) cmp:%float%
           (datum-name-as-string (first (bir:outputs inst)))))
(defvprimop (core::fixnum-to-double :flags (:flushable))
    ((:double-float) :utfixnum) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (%sitofp (in (first (bir:inputs inst))) cmp:%double%
           (datum-name-as-string (first (bir:outputs inst)))))

(defvprimop (core::vector-length :flags (:flushable))
    ((:utfixnum) :object) (inst)
  (assert (= 1 (length (bir:inputs inst))))
  (cmp::gen-vector-length-untagged (in (first (bir:inputs inst)))))

;;; several of these complex-array primops are not yet used, by may be of interest
;;; eventually, so here they remain.

(defvprimop (core::%displacement :flags (:flushable))
    ((:object :utfixnum) :object) (inst)
  (let ((ain (in (first (bir:inputs inst)))))
    (list (cmp:irc-real-array-displacement ain)
          (cmp:irc-real-array-index-offset ain))))

(defvprimop (core::%array-total-size :flags (:flushable))
    ((:utfixnum) :object) (inst)
  (cmp:irc-array-total-size (in (first (bir:inputs inst)))))

(defvprimop (core::%array-rank :flags (:flushable))
    ((:utfixnum) :object) (inst)
  (cmp:irc-array-rank (in (first (bir:inputs inst)))))

(defvprimop (core::%array-dimension :flags (:flushable))
    ((:utfixnum) :object :fixnum) (inst)
  (cmp::irc-array-dimension
   (in (first (bir:inputs inst)))
   (in (second (bir:inputs inst)))))

(defvprimop-intrinsic (core::check-bound :flags (:flushable))
    ((:utfixnum) :object :utfixnum :object)
  "cc_checkBound")

(defun %vector-element-address (vec element-type index)
  (let* ((vtype (cmp::simple-vector-llvm-type element-type))
         (type (llvm-sys:type-get-pointer-to vtype))
         (cvec (cmp:irc-bit-cast vec type))
         (gep-indices (list (%i32 0) (%i32 cmp::+simple-vector-data-slot+) index)))
    (cmp:irc-typed-gep-variable vtype cvec gep-indices)))

(defun element-type->vrtype (element-type)
  (ecase element-type
    ((t) :object)
    ((single-float) :single-float)
    ((double-float) :double-float)
    ((base-char) :base-char)
    ((character) :character)))

(cleavir-primop-info:defprimop core:vref 2 :value :flushable)
(cleavir-primop-info:defprimop core::vset 3 :value :flushable)

(defmethod %primop-rtype-info ((name (eql 'core:vref)) info)
  (let ((vrtype (element-type->vrtype
                 (first (cleavir-primop-info:arguments info)))))
    `((,vrtype) :object :utfixnum)))
(defmethod %primop-rtype-info ((name (eql 'core::vset)) info)
  (let ((vrtype (element-type->vrtype
                 (first (cleavir-primop-info:arguments info)))))
    `((,vrtype) ,vrtype :object :utfixnum)))

(defmethod translate-primop ((nsym (eql 'core:vref)) inst)
  (destructuring-bind (element-type &optional order)
      (cleavir-primop-info:arguments (bir:info inst))
    (let* ((vec (in (first (bir:inputs inst))))
           (index (in (second (bir:inputs inst))))
           (addr (%vector-element-address vec element-type index))
           (vrtype (element-type->vrtype element-type)))
      (out
       (if order
           (cmp:irc-typed-load-atomic (vrtype->llvm vrtype) addr
                                      :order (cmp::order-spec->order order))
           (cmp:irc-typed-load (vrtype->llvm vrtype) addr))
       (first (bir:outputs inst))))))

(defmethod translate-primop ((nsym (eql 'core::vset)) inst)
  (destructuring-bind (element-type &optional order)
      (cleavir-primop-info:arguments (bir:info inst))
    (let* ((val (in (first (bir:inputs inst))))
           (vec (in (second (bir:inputs inst))))
           (index (in (third (bir:inputs inst))))
           (addr (%vector-element-address vec element-type index)))
      (if order
          (cmp:irc-store-atomic val addr :order (cmp::order-spec->order order))
          (cmp:irc-store val addr))
      ;; Teturn the new value because it's a bit involved to rewrite BIR to use
      ;; a linear datum more than once.
      (out val (first (bir:outputs inst))))))

;;;

(defvprimop (core::fixnum-lognot :flags (:flushable)) ((:fixnum) :fixnum) (inst)
  (let* ((arg (in (first (bir:inputs inst))))
         ;; LLVM does not have a dedicated lognot, and instead
         ;; represents it as xor whatever, -1.
         ;; We want to keep the tag bits zero, so we skip the rigamarole
         ;; by just XORing directly with -4 (or whatever, based on how
         ;; many tag bits we use).
         (other (%i64 (ldb (byte 64 0) (ash -1 cmp:+fixnum-shift+))))
         (label (datum-name-as-string (first (bir:outputs inst)))))
    (cmp:irc-xor arg other label)))

;;; NOTE: 0 & 0, 0 | 0, and 0 ^ 0 are all zero, so these operations all
;;; preserve the zero fixnum tag without any issue.
(macrolet ((deflog2 (name op)
             `(defvprimop (,name :flags (:flushable))
                  ((:fixnum) :fixnum :fixnum) (inst)
                (let ((arg1 (in (first (bir:inputs inst))))
                      (arg2 (in (second (bir:inputs inst)))))
                  (,op arg1 arg2)))))
  (deflog2 core::fixnum-logand cmp:irc-and)
  (deflog2 core::fixnum-logior cmp:irc-or)
  (deflog2 core::fixnum-logxor cmp:irc-xor))

;; Wrapping addition of tagged fixnums.
(defvprimop (core::fixnum-add :flags (:flushable))
    ((:fixnum) :fixnum :fixnum) (inst)
  (let ((arg1 (in (first (bir:inputs inst))))
        (arg2 (in (second (bir:inputs inst)))))
    (cmp:irc-add arg1 arg2)))
(defvprimop (core::fixnum-sub :flags (:flushable))
    ((:fixnum) :fixnum :fixnum) (inst)
  (let ((arg1 (in (first (bir:inputs inst))))
        (arg2 (in (second (bir:inputs inst)))))
    (cmp:irc-sub arg1 arg2)))

;;; debugging: check whether a fixnum addition overflows
#+(or)
(defvprimop (core::fixnum-add-overflowp :flags (:flushable))
    ((:boolean) :fixnum :fixnum) (inst)
  (let* ((arg1 (in (first (bir:inputs inst))))
         (arg2 (in (second (bir:inputs inst))))
         (r (%intrinsic-call "llvm.sadd.with.overflow.i64" (list arg1 arg2))))
    (cmp:irc-extract-value r '(1))))

(defvprimop (core::fixnum-add-over :flags (:flushable))
    ((:object) :fixnum :fixnum) (inst)
  (let* ((arg1 (in (first (bir:inputs inst))))
         (arg2 (in (second (bir:inputs inst))))
         (r (%intrinsic-call "llvm.sadd.with.overflow.i64" (list arg1 arg2)))
         (overflowp (cmp:irc-extract-value r '(1)))
         (overflow-block (cmp:irc-basic-block-create "overflow"))
         (no-overflow-block (cmp:irc-basic-block-create "no-overflow"))
         (after-block (cmp:irc-basic-block-create "after"))
         (_1 (cmp:irc-cond-br overflowp overflow-block no-overflow-block))
         (_2 (cmp:irc-begin-block overflow-block))
         (big (%intrinsic-invoke-if-landing-pad-or-call
               "cc_overflowed_signed_bignum"
               (list (cmp:irc-extract-value r '(0)))))
         ;; Necessary because invoke interposes a block.
         (bigblock (cmp:irc-get-insert-block))
         (_3 (cmp:irc-br after-block))
         (_4 (cmp:irc-begin-block no-overflow-block))
         (fix (cmp:irc-int-to-ptr (cmp:irc-extract-value r '(0)) cmp:%t*%)))
    (declare (ignore _1 _2 _3 _4))
    (cmp:irc-br after-block)
    (cmp:irc-begin-block after-block)
    (let ((phi (cmp:irc-phi cmp:%t*% 2 "sum")))
      (cmp:irc-phi-add-incoming phi big bigblock)
      (cmp:irc-phi-add-incoming phi fix no-overflow-block)
      phi)))

(defvprimop (core::fixnum-sub-over :flags (:flushable))
    ((:object) :fixnum :fixnum) (inst)
  (let* ((arg1 (in (first (bir:inputs inst))))
         (arg2 (in (second (bir:inputs inst))))
         (r (%intrinsic-call "llvm.ssub.with.overflow.i64" (list arg1 arg2)))
         (overflowp (cmp:irc-extract-value r '(1)))
         (overflow-block (cmp:irc-basic-block-create "overflow"))
         (no-overflow-block (cmp:irc-basic-block-create "no-overflow"))
         (after-block (cmp:irc-basic-block-create "after"))
         (_1 (cmp:irc-cond-br overflowp overflow-block no-overflow-block))
         (_2 (cmp:irc-begin-block overflow-block))
         (big (%intrinsic-invoke-if-landing-pad-or-call
               "cc_overflowed_signed_bignum"
               (list (cmp:irc-extract-value r '(0)))))
         (bigblock (cmp:irc-get-insert-block))
         (_3 (cmp:irc-br after-block))
         (_4 (cmp:irc-begin-block no-overflow-block))
         (fix (cmp:irc-int-to-ptr (cmp:irc-extract-value r '(0)) cmp:%t*%)))
    (declare (ignore _1 _2 _3 _4))
    (cmp:irc-br after-block)
    (cmp:irc-begin-block after-block)
    (let ((phi (cmp:irc-phi cmp:%t*% 2 "difference")))
      (cmp:irc-phi-add-incoming phi big bigblock)
      (cmp:irc-phi-add-incoming phi fix no-overflow-block)
      phi)))

;; And multiplication. We just avoid tag bits.
;; We could possibly be a bit faster in some situations by shifting
;; in different ways. TODO.
(defvprimop (core::fixnum-mul :flags (:flushable))
    ((:utfixnum) :utfixnum :utfixnum) (inst)
  (let ((arg1 (in (first (bir:inputs inst))))
        (arg2 (in (second (bir:inputs inst)))))
    (cmp:irc-mul arg1 arg2 :nsw t :nuw t)))

;; For division we don't need to untag the inputs but do need to
;; shift the quotient.
(defvprimop (core::fixnum-truncate :flags (:flushable))
    ((:utfixnum :fixnum) :fixnum :fixnum) (inst)
  (let* ((arg1 (in (first (bir:inputs inst))))
         (arg2 (in (second (bir:inputs inst))))
         ;; The LLVM reference doesn't say this very well, but sdiv and srem
         ;; both round towards zero.
         ;; Note that both sdiv and srem are undefined for
         ;; most-negative-fixnum/-1 as that would overflow.
         ;; They're also undefined for zero divisors. Don't do these things.
         (quo (cmp:irc-sdiv arg1 arg2))
         (rem (cmp:irc-srem arg1 arg2)))
    (list quo rem)))
(defvprimop (core::fixnum-rem :flags (:flushable))
    ((:fixnum) :fixnum :fixnum) (inst)
  (let* ((arg1 (in (first (bir:inputs inst))))
         (arg2 (in (second (bir:inputs inst)))))
    (cmp:irc-srem arg1 arg2)))

(macrolet ((def-fixnum-compare (name op)
             `(progn
                (deftprimop ,name (:fixnum :fixnum)
                  (inst)
                  (assert (= (length (bir:inputs inst)) 2))
                  ;; NOTE: We do not HAVE to cast to an integer type,
                  ;; as icmp works fine on pointers directly.
                  ;; However, LLVM doesn't seem to be very intelligent
                  ;; about pointer comparisons, e.g. it does not fold
                  ;; them even when both arguments are inttoptr of
                  ;; constants. So we use the fixnum rtype.
                  (let ((i1 (in (first (bir:inputs inst))))
                        (i2 (in (second (bir:inputs inst)))))
                    (,op i1 i2))))))
  (def-fixnum-compare core::two-arg-fixnum-=  cmp:irc-icmp-eq)
  (def-fixnum-compare core::two-arg-fixnum-<  cmp:irc-icmp-slt)
  (def-fixnum-compare core::two-arg-fixnum-<= cmp:irc-icmp-sle)
  (def-fixnum-compare core::two-arg-fixnum->  cmp:irc-icmp-sgt)
  (def-fixnum-compare core::two-arg-fixnum->= cmp:irc-icmp-sge))

(defvprimop (core::fixnum-positive-logcount :flags (:flushable))
    ((:utfixnum) :fixnum) (inst)
  (let ((arg (in (first (bir:inputs inst)))))
    ;; NOTE we do not need to shift the argument: the tag is all zero
    ;; so it won't affect the population count.
    (%intrinsic-call "llvm.ctpop.i64" (list arg))))

(defvprimop (core::fixnum-shl :flags (:flushable))
    ((:fixnum) :fixnum :utfixnum) (inst)
  (let ((int (in (first (bir:inputs inst))))
        ;; NOTE: shift must be 0-63 inclusive or shifted is poison.
        (shift (in (second (bir:inputs inst)))))
    (cmp:irc-shl int shift :nuw t :nsw t)))

(defvprimop (core::fixnum-ashr :flags (:flushable))
    ((:fixnum) :fixnum :utfixnum) (inst)
  (let* ((int (in (first (bir:inputs inst))))
         ;; NOTE: shift must be 0-63 inclusive or shifted is poison!
         (shift (in (second (bir:inputs inst))))
         (shifted (cmp:irc-ashr int shift))
         (demask (%i64 (ldb (byte 64 0) (lognot cmp:+fixnum-mask+))))
         ;; zero the tag bits
         (fixn (cmp:irc-and shifted demask
                            (datum-name-as-string
                             (first (bir:outputs inst))))))
    fixn))
;;; ditto the above, but makes sure the shift is valid by taking the min.
(defvprimop (core::fixnum-ashr-min :flags (:flushable))
    ((:fixnum) :fixnum :utfixnum) (inst)
  (let* ((int (in (first (bir:inputs inst))))
         ;; NOTE: treated as unsigned, so it'd better be positive
         (pshift (in (second (bir:inputs inst))))
         (shift (%intrinsic-call "llvm.umin.i64" (list pshift (%i64 63))))
         (shifted (cmp:irc-ashr int shift))
         (demask (%i64 (ldb (byte 64 0) (lognot cmp:+fixnum-mask+))))
         (fixn (cmp:irc-and shifted demask)))
    fixn))

;;; Primops for debugging

(defeprimop core:set-breakstep () (inst)
  (%intrinsic-call "cc_set_breakstep" ()))

(defeprimop core:unset-breakstep () (inst)
  (%intrinsic-call "cc_unset_breakstep" ()))

;;; Atomics
;;; These have a sham first input for the order.
;;; FIXME: Make that actually unused so it can be deleted properly.

(defeprimop mp:fence (:object) (inst)
  (destructuring-bind (order)
      (cleavir-primop-info:arguments (bir:info inst))
    (cmp::gen-fence order)))

(defmethod %primop-rtype-info ((name (eql 'core:acas)) info)
  (let ((vrtype (element-type->vrtype
                 (second (cleavir-primop-info:arguments info)))))
    `((,vrtype) :object ,vrtype ,vrtype :object :utfixnum)))

(defvprimop core:acas ((:object) :object :object :object :object :utfixnum)
  (inst)
  (destructuring-bind (order etype rank)
      (cleavir-primop-info:arguments (bir:info inst))
    (assert (and (eql etype 't) (eql rank 1)))
    (let* ((old (in (second (bir:inputs inst))))
           (new (in (third (bir:inputs inst))))
           (vec (in (fourth (bir:inputs inst))))
           (index (in (fifth (bir:inputs inst))))
           (addr (%vector-element-address vec etype index)))
      (cmp:irc-cmpxchg addr old new :order (cmp::order-spec->order order)))))

;;; Type tests

(macrolet ((def-simple-predicate (name mask tag)
             `(deftprimop ,name (:object) (inst)
                (assert (= (length (bir:inputs inst)) 1))
                (cmp:tag-check-cond (in (first (bir:inputs inst)))
                                    ,mask ,tag))))
  (def-simple-predicate core:fixnump cmp:+fixnum-mask+ cmp:+fixnum00-tag+)
  (def-simple-predicate consp cmp:+immediate-mask+ cmp:+cons-tag+)
  (def-simple-predicate characterp cmp:+immediate-mask+ cmp:+character-tag+)
  (def-simple-predicate core:single-float-p
    cmp:+immediate-mask+ cmp:+single-float-tag+)
  (def-simple-predicate core:generalp cmp:+immediate-mask+ cmp:+general-tag+))

(cleavir-primop-info:defprimop core::headerq 1 :value :flushable)

(defmethod %primop-rtype-info ((name (eql 'core::headerq)) info)
  (declare (ignore info))
  '((:boolean) :object))

(defmethod translate-primop ((name (eql 'core::headerq)) inst)
  (destructuring-bind (type)
      (cleavir-primop-info:arguments (bir:info inst))
  ;; We can only actually look at the header value if we have a general,
  ;; so we have to use a phi.
  ;; LLVM's jump-threading analysis ought to take care of the if-if.
  (let ((curb (cmp:irc-get-insert-block))
        (hedb (cmp:irc-basic-block-create "headerq-check"))
        (merge (cmp:irc-basic-block-create "headerq-merge"))
        (in (in (first (bir:inputs inst))))
        (header-info (gethash type core:+type-header-value-map+)))
    (unless (typep header-info '(or integer cons))
      (error "BUG: headerq for unknown type: ~a" type))
    (cmp:compile-tag-check in cmp:+immediate-mask+ cmp:+general-tag+
                           hedb merge)
    (cmp:irc-begin-block hedb)
    (let ((hedp (cmp:header-check-cond header-info in)))
      (cmp:irc-br merge)
      (cmp:irc-begin-block merge)
      (let ((phi (cmp:irc-phi cmp:%i1% 2 "headerq-check")))
        (cmp:irc-phi-add-incoming phi (%i1 0) curb)
        (cmp:irc-phi-add-incoming phi hedp hedb)
        (out phi (first (bir:outputs inst))))))))
