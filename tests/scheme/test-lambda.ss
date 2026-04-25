; tests/scheme/test-lambda.ss

; 1. 简单 lambda 调用
(display ((lambda (x) (+ x 1)) 41))
(newline)

; 2. 嵌套调用
(display (* 2 (+ 3 4)))
(newline)

; 3. 多参数
(display ((lambda (a b c) (+ a b c)) 10 20 30))
(newline)

; 4. 组合 define 函数
(define (square x) (* x x))
(define (add1 x) (+ x 1))
(display (add1 (square 5)))
(newline)

; 5. 条件逻辑
(define (abs x) (if (< x 0) (- x) x))
(display (abs -5))
(newline)
(display (abs 3))
(newline)
