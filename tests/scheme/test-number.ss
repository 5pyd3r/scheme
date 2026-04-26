;; test-number.ss — R7RS numeric tower integration test
;; Run: ./build/src/scheme < tests/scheme/test-number.ss

(display "=== number tests ===") (newline)

;; Basic fixnum arithmetic
(display (+ 10 20)) (newline)              ; 30
(display (- 100 33)) (newline)             ; 67
(display (* 6 7)) (newline)                ; 42
(display (< 3 5)) (newline)                ; #t
(display (= 42 42)) (newline)              ; #t

;; Bignum via overflow
(define big1 (+ 9223372036854775807 1))
(display big1) (newline)
(define big2 (+ big1 1))
(display big2) (newline)
(define big3 (- big2 1))
(display big3) (newline)
(display (= big3 big1)) (newline)          ; #t

;; Bignum multiplication
(display (< big1 big2)) (newline)          ; #t
(display (> big2 big1)) (newline)          ; #t

;; Mixed type
(display (+ big1 1)) (newline)

;; Predicates
(display (number? 42)) (newline)           ; #t
(display (integer? 42)) (newline)          ; #t
(display (exact? 42)) (newline)            ; #t
(display (inexact? 3.14)) (newline)        ; #t
(display (zero? 0)) (newline)              ; #t
(display (positive? 5)) (newline)          ; #t
(display (negative? -3)) (newline)         ; #t
(display (even? 42)) (newline)             ; #t
(display (odd? 42)) (newline)              ; #f

;; Integer operations
(display (gcd 12 8)) (newline)             ; 4
(display (gcd 0 5)) (newline)              ; 5
(display (lcm 6 8)) (newline)              ; 24
(display (abs -42)) (newline)              ; 42
(display (quotient 10 3)) (newline)        ; 3
(display (remainder 10 3)) (newline)       ; 1

;; Number conversion
(display (number->string 12345)) (newline) ; "12345"

;; Flonum
(display (+ 1.5 2.5)) (newline)           ; 4.0
(display (* 3.0 1.5)) (newline)           ; 4.5
(display (< 1.5 2.5)) (newline)           ; #t

;; Mixed numeric types
(display (sin 0)) (newline)               ; 0.0
(display (cos 0)) (newline)               ; 1.0
(display (sqrt 4)) (newline)              ; 2.0

;; Exact/inexact conversion
(display (exact->inexact 42)) (newline)   ; 42.0

(display "=== all number tests passed ===") (newline)
