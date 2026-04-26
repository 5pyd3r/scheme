; tests/scheme/test-bytevector.ss — test bytevector procedures from lib.scm
; All tests display #t for pass, #f for fail.
; Uses bytevector->u8-list to convert to list for equal? comparison.

; ==================== bytevector-copy ====================

(define bvc1 (bytevector-copy #u8(1 2 3)))
(define bvc1l (bytevector->u8-list bvc1))
(display (equal? bvc1l '(1 2 3)))
(newline)

(define bvc2 (bytevector-copy #u8()))
(define bvc2l (bytevector->u8-list bvc2))
(display (equal? bvc2l '()))
(newline)

; ==================== bytevector-append ====================

(define bva1 (bytevector-append #u8(1 2) #u8(3 4)))
(define bva1l (bytevector->u8-list bva1))
(display (equal? bva1l '(1 2 3 4)))
(newline)

(define bva2 (bytevector-append #u8() #u8(1 2)))
(define bva2l (bytevector->u8-list bva2))
(display (equal? bva2l '(1 2)))
(newline)

(define bva3 (bytevector-append #u8(1 2) #u8()))
(define bva3l (bytevector->u8-list bva3))
(display (equal? bva3l '(1 2)))
(newline)
