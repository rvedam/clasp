(asdf:defsystem #:clasp-analyzer
  :description "Static analyzer for clasp"
  :version "0.0.1"
  :author "Christian Schafmeister <chris.schaf@verizon.net>"
  :licence "LGPL-3.0"
  :depends-on (#:clang-tool
               #:clasp-scraper)
  :serial t
  :components ((:file "packages")
               (:file "clasp-analyzer")))
