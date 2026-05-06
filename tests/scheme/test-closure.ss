; tests/scheme/test-closure.ss — Scheme closure integration tests
; All tests display results, verified by test harness output

; 1. Simple closure via Scheme compiler
(display (((lambda (x) (lambda (y) (+ x y))) 1) 2))
(newline)

; 2. Multiple captured vars
(display (((lambda (x y) (lambda (z) (+ x (+ y z)))) 10 20) 30))
(newline)

; 3. No capture (nfree=0)
(display ((lambda (x) (+ x 1)) 99))
(newline)

; 4. Closure via define (falls back to C compiler)
(define (make-adder n) (lambda (x) (+ x n)))
(display ((make-adder 5) 10))
(newline)

; 5. Shadowing — inner param shadows outer
(display (((lambda (x) (lambda (x) (+ x 1))) 100) 200))
(newline)

; 6. Nested closure (3 levels)
(display (((lambda (a) (lambda (b) (lambda (c) (+ a (+ b c))))) 1) 2) 3)
(newline)
