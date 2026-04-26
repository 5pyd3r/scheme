; tests/scheme/test-char.ss — test character procedures from lib.scm
; All tests display #t for pass, #f for fail.

; ==================== char-alphabetic? ====================

(define ca1 (char-alphabetic? #\a))
(display (equal? ca1 #t))
(newline)

(define ca2 (char-alphabetic? #\Z))
(display (equal? ca2 #t))
(newline)

(define ca3 (char-alphabetic? #\0))
(display (equal? ca3 #f))
(newline)

(define ca4 (char-alphabetic? #\space))
(display (equal? ca4 #f))
(newline)

; ==================== char-numeric? ====================

(define cn1 (char-numeric? #\5))
(display (equal? cn1 #t))
(newline)

(define cn2 (char-numeric? #\0))
(display (equal? cn2 #t))
(newline)

(define cn3 (char-numeric? #\a))
(display (equal? cn3 #f))
(newline)

; ==================== char-whitespace? ====================

(define cw1 (char-whitespace? #\space))
(display (equal? cw1 #t))
(newline)

(define cw2 (char-whitespace? #\newline))
(display (equal? cw2 #t))
(newline)

(define cw3 (char-whitespace? #\tab))
(display (equal? cw3 #t))
(newline)

(define cw4 (char-whitespace? #\a))
(display (equal? cw4 #f))
(newline)

; ==================== char-upper? ====================

(define cu1 (char-upper? #\A))
(display (equal? cu1 #t))
(newline)

(define cu2 (char-upper? #\a))
(display (equal? cu2 #f))
(newline)

(define cu3 (char-upper? #\0))
(display (equal? cu3 #f))
(newline)

; ==================== char-lower? ====================

(define cl1 (char-lower? #\a))
(display (equal? cl1 #t))
(newline)

(define cl2 (char-lower? #\A))
(display (equal? cl2 #f))
(newline)

; ==================== char-digit? ====================

(define cd1 (char-digit? #\9))
(display (equal? cd1 #t))
(newline)

(define cd2 (char-digit? #\a))
(display (equal? cd2 #f))
(newline)

; ==================== char-upcase ====================

(define cup1 (char-upcase #\a))
(display (equal? cup1 #\A))
(newline)

(define cup2 (char-upcase #\A))
(display (equal? cup2 #\A))
(newline)

(define cup3 (char-upcase #\0))
(display (equal? cup3 #\0))
(newline)

; ==================== char-downcase ====================

(define cdown1 (char-downcase #\A))
(display (equal? cdown1 #\a))
(newline)

(define cdown2 (char-downcase #\a))
(display (equal? cdown2 #\a))
(newline)

(define cdown3 (char-downcase #\0))
(display (equal? cdown3 #\0))
(newline)

; ==================== char-ci=? ====================

(define cceq1 (char-ci=? #\a #\A))
(display (equal? cceq1 #t))
(newline)

(define cceq2 (char-ci=? #\a #\b))
(display (equal? cceq2 #f))
(newline)

; ==================== char-ci<? ====================

(define cclt1 (char-ci<? #\a #\B))
(display (equal? cclt1 #t))
(newline)

(define cclt2 (char-ci<? #\C #\a))
(display (equal? cclt2 #f))
(newline)
