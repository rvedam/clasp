
#+clasp(select-package :core)


#-clasp(load "backquote.lsp")


(trace bq-simplify bq-simplify-args bq-attach-append bq-attach-conses bq-remove-tokens)
(bq '(list (comma (+ 1 2))))


quote
