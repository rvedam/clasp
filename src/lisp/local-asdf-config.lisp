;;;
;;; Configure ASDF with directories in under SYS:
;;;
(require :asdf)
(format t "Configuring ASDF for local directories~%")
(defun all-subdirs (dir)
  (let (dirs)
    (labels ((trav (d)
		   (dolist (d (uiop:subdirectories d))
		     (push d dirs)
		     (trav d))))
      (trav dir))
    dirs))

(let ((dirs (all-subdirs (translate-logical-pathname #P"sys:"))))
  (dolist (dir dirs)
    (push dir asdf:*central-registry*))
  (format t "Added ~a subdirectories of ~a to ASDF:*CENTRAL-REGISTRY*~%"
          (length dirs) (translate-logical-pathname #P"sys:")))



