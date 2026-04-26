; tests/scheme/test-basic.ss
(display "Hello from Scheme!")
(newline)

; Test lambda call
(display ((lambda (x) (+ x 1)) 41))
(newline)

; Test define with lambda
(define double (lambda (x) (* x 2)))
(display (double 21))
(newline)

; Test define chain syntax
(define (triple x) (* x 3))
(display (triple 14))
(newline)
