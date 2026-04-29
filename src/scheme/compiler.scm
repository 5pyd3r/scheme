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
                                                                                              #f)))))))))))))))))))))))

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
                              (_cb-mark-error! cb)
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

;; === Macro system (in compiler.scm for Phase 1a loading) ===
(define _macro-id-cell (cons 0 '()))

(define (_immune? sym)
  (if (symbol? sym)
      (if (eq? sym 'lambda) #t
          (if (eq? sym 'if) #t
              (if (eq? sym 'define) #t
                  (if (eq? sym 'set!) #t
                      (if (eq? sym 'begin) #t
                          (if (eq? sym 'quote) #t
                              (if (eq? sym 'cond) #t
                                  (if (eq? sym 'let) #t
                                      (if (eq? sym 'else) #t
                                          (if (eq? sym 'define-syntax) #t
                                              (if (eq? sym 'syntax-rules) #t
                                                  (if (eq? sym '...) #t
                                                      (if (prim-index sym) #t #f)))))))))))))
      #f))

(define (_match-pat pat input literals)
  (if (if (symbol? pat) (memq pat literals) #f)
      (if (eq? pat input) '() #f)
      (if (symbol? pat)
          (if (eq? pat '...) '() (list (cons pat input)))
          (if (pair? pat)
              (if (pair? input) (_match-pair pat input literals) #f)
              (if (null? pat)
                  (if (null? input) '() #f)
                  (if (eqv? pat input) '() #f))))))

(define (_match-pair pat input literals)
  (if (if (pair? (cdr pat)) (if (null? (cdr (cdr pat))) (if (eq? (car (cdr pat)) '...) (if (symbol? (car pat)) (if (memq (car pat) literals) #f #t) #f) #f) #f) #f)
      (list (cons (car pat) input))
      (if (_match-pat (car pat) (car input) literals)
          (if (_match-pat (cdr pat) (cdr input) literals)
              (_append-alist (_match-pat (car pat) (car input) literals) (_match-pat (cdr pat) (cdr input) literals))
              #f)
          #f)))

(define (_append-alist a b)
  (if (null? a) b (cons (car a) (_append-alist (cdr a) b))))

(define (_rename-sym sym rename-id) sym)

(define (_fill-template tmpl bindings rename-id)
  (if (symbol? tmpl)
      (if (eq? tmpl '...) tmpl
          (if (_immune? tmpl) tmpl
              (if (assq tmpl bindings)
                  (cdr (assq tmpl bindings))
                  (_rename-sym tmpl rename-id))))
      (if (pair? tmpl)
          (if (if (pair? (cdr tmpl)) (if (null? (cdr (cdr tmpl))) (eq? (car (cdr tmpl)) '...) #f) #f)
              (_fill-ellipsis (car tmpl) bindings rename-id)
              (cons (_fill-template (car tmpl) bindings rename-id)
                    (_fill-template (cdr tmpl) bindings rename-id)))
          tmpl)))

(define (_fill-ellipsis inner-tmpl bindings rename-id)
  (if (pair? (cdr (assq (_ellipsis-var inner-tmpl) bindings)))
      (_fill-ellipsis-iter inner-tmpl (_ellipsis-var inner-tmpl) (cdr (assq (_ellipsis-var inner-tmpl) bindings)) bindings rename-id)
      '()))

(define (_ellipsis-var tmpl)
  (if (symbol? tmpl) tmpl
      (if (pair? tmpl) (_ellipsis-var (car tmpl)) #f)))

(define (_fill-ellipsis-iter tmpl var vals bindings rename-id)
  (if (null? vals) '()
      (cons (_fill-template tmpl (cons (cons var (car vals)) bindings) rename-id)
            (_fill-ellipsis-iter tmpl var (cdr vals) (cons (cons var (car vals)) bindings) rename-id))))

(define (_try-clauses form clauses literals rename-id)
  (if (null? clauses)
      form
      (if (_match-pat (car (car clauses)) form literals)
          (_fill-template (car (cdr (car clauses))) (_match-pat (car (car clauses)) form literals) rename-id)
          (_try-clauses form (cdr clauses) literals rename-id))))

(define (_make-transformer-apply form macro-id clauses literals call-id-cell)
  (_try-clauses form clauses literals (+ (* macro-id 1000) (car call-id-cell))))

(define (_make-transformer macro-id clauses literals call-id-cell)
  (lambda (form)
    (_make-transformer-apply form macro-id clauses literals call-id-cell)))

(define (_sr-helper macro-id clauses literals)
  (_make-transformer macro-id clauses literals (cons 0 '())))

(define (_sr-increment-and-create clauses literals)
  (begin
    (set-car! _macro-id-cell (+ (car _macro-id-cell) 1))
    (_sr-helper (- (car _macro-id-cell) 1) clauses literals)))

(define syntax-rules
  (lambda (literals clauses)
    (_sr-increment-and-create clauses literals)))

;; === Main entry ===
(define (compile expr) (let ((cb (_make-cb)) (cs (_make-consts))) (_compile-expr expr cb cs) (if (_cb-is-error? cb) #f (begin (_emit-byte! cb 255) (cons (_cb->list cb) (_cs->list cs))))))
