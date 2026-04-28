(define OP-PUSH-INT 5) (define OP-PUSH-CONST 4) (define OP-HALT 255)
(define OP-PUSH-NIL 1) (define OP-PUSH-TRUE 2) (define OP-PUSH-FALSE 3)
(define OP-POP 6) (define OP-LREF 16) (define OP-GREF 20) (define OP-GSET 21)
(define OP-CLOSE 32) (define OP-CALL 33) (define OP-RETURN 36)
(define OP-JMP 48) (define OP-JMP-IF-NOT 50) (define OP-PRIM-CALL 80)

(define (_make-cb) (let ((dummy (cons 0 '()))) (cons dummy dummy)))
(define (_emit-byte! cb b) (let ((n (cons b '()))) (set-cdr! (cdr cb) n) (set-cdr! cb n)))
(define (_list-len p n) (if (null? p) n (_list-len (cdr p) (+ n 1))))
(define (_cb-pos cb) (_list-len (cdr (car cb)) 0))
(define (_list-set! p i b) (if (= i 0) (set-car! p b) (_list-set! (cdr p) (- i 1) b)))
(define (_cb-patch! cb pos b) (_list-set! (cdr (car cb)) pos b))
(define (_cb->list cb) (cdr (car cb)))
(define (_cb-mark-error! cb) (set-car! (car cb) 1))
(define (_cb-is-error? cb) (= (car (car cb)) 1))
(define (_make-consts) (let ((dummy (cons 0 '()))) (cons dummy dummy)))
(define (_add-const! cs val) (let ((n (cons val '()))) (set-cdr! (cdr cs) n) (set-cdr! cs n)) (- (_cb-pos cs) 1))
(define (_cs->list cs) (cdr (car cs)))
(define (_count-exprs lst) (if (null? lst) 0 (+ 1 (_count-exprs (cdr lst)))))
(define (_patch-jmp cb jmp-pos target) (let ((off (- target (+ jmp-pos 3)))) (let ((u (if (< off 0) (+ off 65536) off))) (_cb-patch! cb (+ jmp-pos 1) (remainder u 256)) (_cb-patch! cb (+ jmp-pos 2) (quotient u 256)))))

(define (_compile-args args cb cs) (if (null? args) 0 (begin (_compile-args (cdr args) cb cs) (_compile-expr (car args) cb cs))))

;; _is-handled? — returns #t if the compiler handles this form directly
(define (_is-handled? fn)
  (if (eq? fn 'begin) #t
      (if (eq? fn 'cons) #t
          (if (eq? fn 'car) #t
              (if (eq? fn 'cdr) #t
                  (if (eq? fn 'null?) #t
                      (if (eq? fn 'pair?) #t
                          (if (eq? fn 'eq?) #t
                              (if (eq? fn 'eqv?) #t
                                  (if (eq? fn '+) #t
                                      (if (eq? fn '-) #t
                                          (if (eq? fn '*) #t
                                              (if (eq? fn '/) #t
                                                  (if (eq? fn '<) #t
                                                      (if (eq? fn '>) #t
                                                          (if (eq? fn '=) #t
                                                              (if (eq? fn 'display) #t
                                                                  (if (eq? fn 'newline) #t
                                                                      (if (eq? fn 'remainder) #t
                                                                          (if (eq? fn 'quotient) #t
                                                                              (if (eq? fn 'prim-index) #t
                                                                                  (if (eq? fn 'assemble-code) #t
                                                                                      (if (eq? fn 'find-global-slot) #t
                                                                                          (if (eq? fn 'define-syntax) #t
                                                                                              #f))))))))))))))))))))))))

;; _lookup-macro — returns transformer or #f if not a macro
(define (_lookup-macro name)
  (_assq-lookup name (car *macro-table*)))

(define (_assq-lookup key alist)
  (if (null? alist) #f
      (if (eq? key (car (car alist))) (cdr (car alist))
          (_assq-lookup key (cdr alist)))))

(define (_compile-expr expr cb cs)
  (if (fixnum? expr) (begin (_emit-byte! cb OP-PUSH-INT) (_emit-byte! cb (remainder expr 256)) (_emit-byte! cb (remainder (quotient expr 256) 256)) (_emit-byte! cb (remainder (quotient expr 65536) 256)) (_emit-byte! cb (remainder (quotient expr 16777216) 256)))
      (if (null? expr) (_emit-byte! cb OP-PUSH-NIL)
          (if (eq? expr #t) (_emit-byte! cb OP-PUSH-TRUE)
              (if (eq? expr #f) (_emit-byte! cb OP-PUSH-FALSE)
                  (if (pair? expr)
                      (if (eq? (car expr) 'define-syntax)
                          (_compile-define-syntax (cdr expr) cb cs)
                          (if (_lookup-macro (car expr))
                              (_compile-expr ((_lookup-macro (car expr)) expr) cb cs)
                              (if (_is-handled? (car expr))
                                  (if (eq? (car expr) 'begin) (_compile-begin (cdr expr) cb cs)
                                      (begin (_compile-args (cdr expr) cb cs) (_emit-byte! cb OP-PRIM-CALL) (_emit-byte! cb (_count-exprs (cdr expr))) (_emit-byte! cb (remainder (prim-index (car expr)) 256)) (_emit-byte! cb (quotient (prim-index (car expr)) 256))))
                                  (_cb-mark-error! cb))))
                      (if (symbol? expr) (begin (_emit-byte! cb OP-GREF) (_emit-byte! cb (find-global-slot expr)))
                          (begin (_emit-byte! cb OP-PUSH-CONST) (_emit-byte! cb (_add-const! cs expr))))))))))

(define (_compile-define-syntax args cb cs)
  (_emit-byte! cb OP-PUSH-NIL))

(define (_compile-begin args cb cs) (if (null? args) (_emit-byte! cb OP-PUSH-NIL) (_compile-begin-1 args cb cs)))
(define (_compile-begin-1 args cb cs) (if (null? (cdr args)) (_compile-expr (car args) cb cs) (begin (_compile-expr (car args) cb cs) (_emit-byte! cb OP-POP) (_compile-begin-1 (cdr args) cb cs))))

(define (compile expr) (let ((cb (_make-cb)) (cs (_make-consts))) (_compile-expr expr cb cs) (if (_cb-is-error? cb) #f (begin (_emit-byte! cb 255) (cons (_cb->list cb) (_cs->list cs))))))
