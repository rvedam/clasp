(in-package :core)

;;
;; Simple regression testing macros
;;
(defmacro test (exp &optional name)
  (let ((result (gensym)))
    `(let ((,result ,exp))
       (if ,result
	   (if ,name
	       (bformat t "PASSED %s\n" ,name)
	       nil)
	   (progn
	     (bformat t "FAILED ")
	     (if ,name (bformat t "%s" ,name))
	     (bformat t " -> %s\n" ',exp)))
       ,result)))



(defmacro test-equal (exp result &optional name)
  `(if (not (test (equal ,exp ,result) ,name))
       (bformat t "ne %s %s\n" ',exp ',result)))


(export '(test test-equal))
