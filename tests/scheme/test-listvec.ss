; tests/scheme/test-listvec.ss — test list and vector procedures from lib.scm
; All tests just display #t for pass, #f for fail.
; Avoid let/define-recursion/function-composition (Scheme compiler bugs).

; ==================== list? ====================

(define l1 '(a b c))
(define l1? (list? l1))
(display l1?)
(newline)

(define l2 '())
(define l2? (list? l2))
(display l2?)
(newline)

(define l3 42)
(define l3? (list? l3))
(display l3?)
(newline)

(define l4 (cons 1 2))
(define l4? (list? l4))
(display l4?)
(newline)

; ==================== list-copy ====================

(define lc-src '(1 2 3))
(define lc-cpy (list-copy lc-src))
(display (equal? lc-src lc-cpy))
(newline)

; ==================== list-tail ====================

(define lt1 (list-tail '(a b c d) 2))
(display (equal? lt1 '(c d)))
(newline)

(define lt2 (list-tail '(a b c d) 0))
(display (equal? lt2 '(a b c d)))
(newline)

; ==================== list-ref ====================

(define lr1 (list-ref '(a b c d) 2))
(display (equal? lr1 'c))
(newline)

; ==================== list-set! ====================

(define larg '(1 2 3))
(list-set! larg 1 'x)
(define lr2 (list-ref larg 1))
(display (equal? lr2 'x))
(newline)

; ==================== memq / memv / member ====================

(define m1 (memq 'b '(a b c)))
(display (equal? m1 '(b c)))
(newline)

(define m2 (memq 'd '(a b c)))
(display (equal? m2 #f))
(newline)

(define m3 (memv 2 '(1 2 3)))
(display (equal? m3 '(2 3)))
(newline)

; ==================== assq / assv / assoc ====================

(define a1 (assq 'a '((a 1) (b 2))))
(display (equal? a1 '(a 1)))
(newline)

(define a2 (assq 'c '((a 1) (b 2))))
(display (equal? a2 #f))
(newline)

; ==================== map / for-each / filter ====================

(define mp1 (map (lambda (x) (+ x 1)) '(1 2 3)))
(display (equal? mp1 '(2 3 4)))
(newline)

(define fa (filter (lambda (x) (> x 2)) '(1 2 3 4 5)))
(display (equal? fa '(3 4 5)))
(newline)

; ==================== vector-fill! ====================

(define vf (make-vector 3 'a))
(vector-fill! vf 'z)
(define vf0 (vector-ref vf 0))
(define vf2 (vector-ref vf 2))
(display (equal? vf0 'z))
(newline)
(display (equal? vf2 'z))
(newline)

; ==================== vector-copy ====================

(define vc (vector-copy (vector 1 2 3)))
(define vc0 (vector-ref vc 0))
(define vc2 (vector-ref vc 2))
(display (equal? vc0 1))
(newline)
(display (equal? vc2 3))
(newline)

; ==================== vector-append ====================

(define va (vector-append (vector 1 2) (vector 3 4)))
(define va-len (vector-length va))
(define va0 (vector-ref va 0))
(define va2 (vector-ref va 2))
(display (equal? va-len 4))
(newline)
(display (equal? va0 1))
(newline)
(display (equal? va2 3))
(newline)

; ==================== vector-map ====================

(define vm (vector-map (lambda (x) (* x 10)) (vector 1 2 3)))
(define vm1 (vector-ref vm 1))
(display (equal? vm1 20))
(newline)
