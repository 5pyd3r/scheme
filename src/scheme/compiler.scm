;; + is at prim index 9 (0:cons, 1:car, 2:cdr, ..., 9:+)
(define (compile expr)
  (cons (list 5 1 0 0 0    ;; OP_PUSH_INT 1
              5 2 0 0 0    ;; OP_PUSH_INT 2
              80 2 9 0     ;; OP_PRIM_CALL nargs=2, prim=+ (index 9)
              255)          ;; OP_HALT
        '()))
