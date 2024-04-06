(in-package :cscrape)

(defun gather-source-files (tags)
  (let ((source-files (make-hash-table :test #'equal)))
    (loop for tag in tags
         do (when (typep tag 'tags:source-tag)
              (let* ((source-file (tags:file% tag))
                     (entries (gethash source-file source-files)))
                (push tag entries)
                (setf (gethash source-file source-files) entries))))
    (maphash (lambda (file entries)
               (let ((sorted-entries (sort entries #'< :key #'tags:line%)))
                 (setf (gethash file source-files) sorted-entries)))
             source-files)
    source-files))

(defun calculate-character-offsets-one-file (source-file tags)
  (declare (optimize debug))
  (let ((cur-line 0)
        (char-offset 0)
        (prev-char-offset 0))
    (with-open-file (fin (if (uiop:relative-pathname-p source-file)
                             source-file
                             (merge-pathnames (enough-namestring source-file *clasp-sys*) *clasp-code*))
                     :direction :input :external-format :utf-8)
      (dolist (tag tags)
        (let ((search-line (tags:line% tag)))
          (loop
             (if (>= cur-line search-line)
                 (progn
                   (setf (tags:character-offset% tag) (1+ prev-char-offset))
                   (return nil))
                 (let ((l (read-line fin nil :eof)))
                   (when (eq l :eof)
                     (error "While searching for character offset for tag: ~a at line ~d we hit the bottom of the file - it was not found?~%    This means an old .sif file with out of date information everything needs to be rebuilt.  Try ./waf distclean"
                            tag search-line))
                   (incf cur-line)
                   (setq prev-char-offset char-offset)
                   (setq char-offset (+ (length l) 1 char-offset))))))))))

(defun calculate-character-offsets (source-file-ht)
  (maphash (lambda (source-file tags)
             (calculate-character-offsets-one-file source-file tags))
           source-file-ht))
