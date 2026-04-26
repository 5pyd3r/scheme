; tests/scheme/test-string.ss — test string procedures from lib.scm
; All tests display #t for pass, #f for fail.

; ==================== string-copy ====================

(define scp1 (string-copy "hello"))
(define scp1? (string=? scp1 "hello"))
(display scp1?)
(newline)

(define scp2 (string-copy ""))
(define scp2? (string=? scp2 ""))
(display scp2?)
(newline)

; ==================== string-append ====================

(define sapp1 (string-append "abc" "def"))
(define sapp1? (string=? sapp1 "abcdef"))
(display sapp1?)
(newline)

(define sapp2 (string-append "" "xyz"))
(define sapp2? (string=? sapp2 "xyz"))
(display sapp2?)
(newline)

(define sapp3 (string-append "xyz" ""))
(define sapp3? (string=? sapp3 "xyz"))
(display sapp3?)
(newline)

; ==================== string-upcase ====================

(define sup1 (string-upcase "Hello"))
(define sup1? (string=? sup1 "HELLO"))
(display sup1?)
(newline)

(define sup2 (string-upcase "abc"))
(define sup2? (string=? sup2 "ABC"))
(display sup2?)
(newline)

(define sup3 (string-upcase ""))
(define sup3? (string=? sup3 ""))
(display sup3?)
(newline)

; ==================== string-downcase ====================

(define sdown1 (string-downcase "HELLO"))
(define sdown1? (string=? sdown1 "hello"))
(display sdown1?)
(newline)

(define sdown2 (string-downcase "ABC"))
(define sdown2? (string=? sdown2 "abc"))
(display sdown2?)
(newline)

(define sdown3 (string-downcase ""))
(define sdown3? (string=? sdown3 ""))
(display sdown3?)
(newline)
