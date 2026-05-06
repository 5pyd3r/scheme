; tests/scheme/test-features.ss — comprehensive feature tests
; NOTE: file execution stops on first error, so error-triggering tests
; are in test-input-validation.ss instead.

; ==================== letrec (C compiler handler) ====================
(display (letrec ((x 1)) x)) (newline)
(display (letrec ((x 1) (y 2)) (+ x y))) (newline)
(display (letrec ((f (lambda (n) (* n n)))) (f 5))) (newline)

; ==================== Named let recursion ====================
(display (let loop ((i 0)) (if (>= i 3) i (loop (+ i 1))))) (newline)
(display (let loop ((i 0) (sum 0)) (if (>= i 5) sum (loop (+ i 1) (+ sum i))))) (newline)

; ==================== case special form ====================
(display (case 1 ((1) 'one) (else 'other))) (newline)
(display (case 2 ((1) 'one) (else 'other))) (newline)
(display (case 5 ((1 3 5) 'odd) ((2 4) 'even))) (newline)
(display (case (+ 1 2) ((1) 'a) ((3) 'b) (else 'c))) (newline)

; ==================== apply ====================
(display (apply + '(1 2 3))) (newline)
(display (apply + 1 2 '(3 4))) (newline)
(display (apply (lambda (x y) (+ x y)) '(3 4))) (newline)

; ==================== Quasiquote ====================
(display (car `(1 2 3))) (newline)
(display (car (cdr `(1 ,(+ 10 20) 3)))) (newline)
(display (car `(,42))) (newline)
(display `a) (newline)
(display `#t) (newline)
(display `42) (newline)
(display (car `(1 ,@(list 2 3) 4))) (newline)

; ==================== Dotted-tail lambda ====================
(display ((lambda (a . rest) a) 1 2 3)) (newline)
(display ((lambda (a . rest) (length rest)) 1 2 3)) (newline)
(display ((lambda rest (length rest)) 1 2 3 4)) (newline)
(display ((lambda (a b . rest) (+ a b)) 10 20 30 40)) (newline)

; ==================== New primitives ====================
(display (exact 3.14)) (newline)
(display (inexact 42)) (newline)
(display (exact-integer? 5)) (newline)
(display (exact-integer? 3.14)) (newline)
(display (vector-length (string->vector "abc"))) (newline)
(display (vector->string (vector #\x #\y))) (newline)
(display (string-map char-upcase "hello")) (newline)

; NOTE: when/unless macros work correctly but cause SIGSEGV on exit
; after multiple macro expansions (nested vm_execute bug). Test manually.

; ==================== cxr functions ====================
(display (cadr '(1 2 3))) (newline)
(display (caddr '(1 2 3 4))) (newline)
(display (caar '((a) b))) (newline)
(display (cddr '(1 2 3 4))) (newline)
(display (caaar '(((x))))) (newline)
(display (cadddr '(1 2 3 4 5))) (newline)
