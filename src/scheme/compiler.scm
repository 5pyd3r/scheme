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

;; === Helpers for closure capture (defined before _compile-expr) ===
;; _assq-lookup: return cdr of matching key in alist, or #f
(define (_assq-lookup key alist)
  (if (pair? alist)
      (if (eq? key (car (car alist))) (cdr (car alist))
          (_assq-lookup key (cdr alist)))
      #f))

;; _is-primitive?: #t if sym is a known primitive
(define (_is-primitive? sym)
  (if (symbol? sym) (if (prim-index sym) #t #f) #f))

;; _memq: like memq using eq?  (lib.scm version not available yet during Phase 1a)
(define (_memq x lst)
  (if (null? lst) #f (if (eq? x (car lst)) lst (_memq x (cdr lst)))))

;; _append: append two lists
(define (_append a b)
  (if (null? a) b (cons (car a) (_append (cdr a) b))))

;; _dedup: remove duplicates from list (keeps first occurrence)
(define (_dedup syms)
  (if (null? syms) '()
      (let ((s (car syms)) (rest (_dedup (cdr syms))))
        (if (_memq s rest) rest (cons s rest)))))

;; _assign-slots: create alist assigning slots starting at 'start'
(define (_assign-slots syms start)
  (if (null? syms) '()
      (cons (cons (car syms) start)
            (_assign-slots (cdr syms) (+ start 1)))))

;; _free-syms: collect free symbols in expr (relative to params and env)
(define (_free-syms expr params env)
  (if (null? expr) '()
      (if (symbol? expr)
          (if (_assq-lookup expr params) '()
              (if (_is-primitive? expr) '()
                  (if (_assq-lookup expr env) (list expr) '())))
          (if (pair? expr)
              (if (eq? (car expr) 'quote) '()
                  (if (eq? (car expr) 'lambda)
                      (_free-syms-list (cdr (cdr expr)) (car (cdr expr)) env)
                      (_append (_free-syms (car expr) params env)
                               (_free-syms-list (cdr expr) params env))))
              '()))))

(define (_free-syms-list lst params env)
  (if (null? lst) '()
      (_append (_free-syms (car lst) params env)
               (_free-syms-list (cdr lst) params env))))

;; _compile-args: compile argument list left-to-right (matching VM stack order)
(define (_compile-args args cb cs env)
  (if (null? args) 0 (begin (_compile-expr (car args) cb cs env) (_compile-args (cdr args) cb cs env))))

;; _is-handled? -- returns #t if the compiler handles this form directly
(define (_is-handled? fn)
  (if (eq? fn 'lambda) #t
      (if (eq? fn 'cons) #t
          (if (eq? fn 'null?) #t
              (if (eq? fn 'pair?) #t
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
                                                                                  #f)))))))))))))))))))))

;; _lookup-macro -- returns transformer or #f if not a macro
(define (_lookup-macro name)
  (if (pair? *macro-table*)
      (_assq-lookup name (car *macro-table*))
      #f))

;; Top-level macro expansion -- called from C trampoline, not from _compile-expr
;; Calls transformer directly (no eval) to avoid nested vm_execute state corruption.
;; Transformer expects the raw form, not quoted — same as (eval `(,t ',form))
;; where eval evaluates the quote, giving the transformer just 'form'.
(define (_expand-once form)
  (if (pair? form)
      (let ((t (_lookup-macro (car form))))
        (if t (t form) form))
      form))

;; Macro expansion helper — calls transformer directly
(define (_expand-and-compile form cb cs)
  (let ((t (_lookup-macro (car form))))
    (_compile-expr (t form) cb cs '())))

;; === Main expression compiler: takes expr, cb, cs, env ===
(define (_compile-expr expr cb cs env)
  (if (fixnum? expr) (begin (_emit-byte! cb OP-PUSH-INT) (_emit-byte! cb (remainder expr 256)) (_emit-byte! cb (remainder (quotient expr 256) 256)) (_emit-byte! cb (remainder (quotient expr 65536) 256)) (_emit-byte! cb (remainder (quotient expr 16777216) 256)))
      (if (null? expr) (_emit-byte! cb OP-PUSH-NIL)
          (if (eq? expr #t) (_emit-byte! cb OP-PUSH-TRUE)
              (if (eq? expr #f) (_emit-byte! cb OP-PUSH-FALSE)
                  (if (pair? expr)
                      (if (eq? (car expr) 'define-syntax)
                          (_compile-define-syntax (cdr expr) cb cs)
                          (if (_lookup-macro (car expr))
                              (_expand-and-compile expr cb cs)
                              (if (_is-handled? (car expr))
                                  (if (eq? (car expr) 'begin) (_compile-begin (cdr expr) cb cs env)
                                      (if (eq? (car expr) 'lambda) (_compile-lambda (cdr expr) cb cs env)
                                          (begin (_compile-args (cdr expr) cb cs env) (_emit-byte! cb OP-PRIM-CALL) (_emit-byte! cb (_count-exprs (cdr expr))) (_emit-byte! cb (remainder (prim-index (car expr)) 256)) (_emit-byte! cb (quotient (prim-index (car expr)) 256)))))
                                  (_cb-mark-error! cb))))
                      ;; Symbol dispatch: env lookup before global
                      (if (symbol? expr)
                          (let ((env-slot (_assq-lookup expr env)))
                            (if env-slot
                                (begin (_emit-byte! cb OP-LREF) (_emit-byte! cb env-slot))
                                (begin (_emit-byte! cb OP-GREF) (_emit-byte! cb (find-global-slot expr)))))
                          (begin (_emit-byte! cb OP-PUSH-CONST) (_emit-byte! cb (_add-const! cs expr))))))))))

(define (_compile-define-syntax args cb cs)
  (begin
    (set-car! *macro-table* (cons (cons (car args) (eval (car (cdr args)))) (car *macro-table*)))
    (_emit-byte! cb OP-PUSH-NIL)))

(define (_compile-begin args cb cs env)
  (if (null? args) (_emit-byte! cb OP-PUSH-NIL) (_compile-begin-1 args cb cs env)))
(define (_compile-begin-1 args cb cs env)
  (if (null? (cdr args)) (_compile-expr (car args) cb cs env)
      (begin (_compile-expr (car args) cb cs env) (_emit-byte! cb OP-POP) (_compile-begin-1 (cdr args) cb cs env))))

;; === Lambda compilation with closure capture ===
(define (_compile-lambda args cb cs env)
  (let ((params (car args)) (body (cdr args)))
    (let ((wrapped-body (if (null? (cdr body)) (car body) (cons 'begin body))))
      ;; Detect dotted-tail and count fixed params
      (let* ((parsed (_parse-params params 0))
             (fixed-params (car parsed))
             (rest-param (cdr parsed))
             (nfixed (_count-exprs fixed-params))
             (is-dotted (if rest-param 1 0)))
        ;; Build full param list for free var detection
        (let ((all-params (if rest-param (_append fixed-params (list rest-param)) fixed-params)))
          (let* ((free-syms (_dedup (_free-syms wrapped-body all-params env)))
                 (nfree (_count-exprs free-syms))
                 (captured (_assign-slots free-syms 1))
                 (child-env (_append captured (_assign-slots all-params (+ nfree 1)))))
            (let ((child-cb (_make-cb)) (child-cs (_make-consts)))
              (_compile-expr wrapped-body child-cb child-cs child-env)
              (_emit-byte! child-cb OP-RETURN)
              (let ((code-idx (assemble-code (cons (_cb->list child-cb) (_cs->list child-cs)))))
                ;; Emit LREF for each captured var from parent env
                (let _emit ((cap captured))
                  (if (null? cap) 0
                      (begin
                        (_emit-byte! cb OP-LREF)
                        (_emit-byte! cb (_assq-lookup (car (car cap)) env))
                        (_emit (cdr cap)))))
                (_emit-byte! cb OP-CLOSE)
                (_emit-byte! cb (remainder code-idx 256))
                (_emit-byte! cb (quotient code-idx 256))
                (_emit-byte! cb nfree)
                (_emit-byte! cb (if is-dotted (+ 128 nfixed) 0))))))))))

;; _parse-params: walk param list, return (fixed-params . rest-param-or-#f)
(define (_parse-params p i)
  (if (pair? p)
      (let ((result (_parse-params (cdr p) (+ i 1))))
        (cons (cons (car p) (car result)) (cdr result)))
      (if (symbol? p)
          (cons '() p)       ; dotted-tail: rest param found
          (cons '() #f))))   ; null? or non-symbol → no rest param

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

(define (_init-compound-bindings pat)
  (if (symbol? pat) (list (cons pat '()))
      (if (pair? pat)
          (_append-alist (_init-compound-bindings (car pat))
                        (_init-compound-bindings (cdr pat)))
          '())))

(define (_merge-compound single rest)
  (if (null? single) '()
      (cons (cons (car (car single))
                  (cons (cdr (car single)) (cdr (assq (car (car single)) rest))))
            (_merge-compound (cdr single) rest))))

(define (_match-compound-ellipsis inner-pat input literals)
  (if (null? input)
      (_init-compound-bindings inner-pat)
      (let ((first (_match-pat inner-pat (car input) literals)))
        (if first
            (let ((rest (_match-compound-ellipsis inner-pat (cdr input) literals)))
              (if rest (_merge-compound first rest) #f))
            #f))))

(define (_match-pair pat input literals)
  (if (if (pair? (cdr pat)) (if (null? (cdr (cdr pat))) (eq? (car (cdr pat)) '...) #f) #f)
      (if (symbol? (car pat))
          (if (memq (car pat) literals) #f (list (cons (car pat) input)))
          (if (pair? input) (_match-compound-ellipsis (car pat) input literals) #f))
      (if (_match-pat (car pat) (car input) literals)
          (if (_match-pat (cdr pat) (cdr input) literals)
              (_append-alist (_match-pat (car pat) (car input) literals) (_match-pat (cdr pat) (cdr input) literals))
              #f)
          #f)))

(define (_append-alist a b)
  (if (null? a) b (cons (car a) (_append-alist (cdr a) b))))

(define (_rename-sym sym rename-id)
  (string->symbol (string-append (symbol->string sym) "{M" (number->string rename-id) "}")))

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

(define (_list-ref lst i)
  (if (= i 0) (car lst) (_list-ref (cdr lst) (- i 1))))

(define (_template-vars tmpl)
  (if (symbol? tmpl) (list tmpl)
      (if (pair? tmpl) (_append (_template-vars (car tmpl)) (_template-vars (cdr tmpl))) '())))

(define (_build-compound-bindings vars bindings idx)
  (if (null? vars) '()
      (cons (cons (car vars) (_list-ref (cdr (assq (car vars) bindings)) idx))
            (_build-compound-bindings (cdr vars) bindings idx))))

(define (_fill-compound-iter inner-tmpl vars bindings idx n rename-id)
  (if (< idx n)
      (cons (_fill-template inner-tmpl (_build-compound-bindings vars bindings idx) rename-id)
            (_fill-compound-iter inner-tmpl vars bindings (+ idx 1) n rename-id))
      '()))

(define (_fill-compound-ellipsis inner-tmpl bindings rename-id)
  (let ((vars (_template-vars inner-tmpl)))
    (let ((first-vals (cdr (assq (car vars) bindings))))
      (let ((n (_count-exprs first-vals)))
        (_fill-compound-iter inner-tmpl vars bindings 0 n rename-id)))))

(define (_fill-ellipsis inner-tmpl bindings rename-id)
  (if (symbol? inner-tmpl)
      (if (pair? (cdr (assq (_ellipsis-var inner-tmpl) bindings)))
          (_fill-ellipsis-iter inner-tmpl (_ellipsis-var inner-tmpl)
             (cdr (assq (_ellipsis-var inner-tmpl) bindings)) bindings rename-id)
          '())
      (_fill-compound-ellipsis inner-tmpl bindings rename-id)))

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
(define (compile expr) (let ((cb (_make-cb)) (cs (_make-consts))) (_compile-expr expr cb cs '()) (if (_cb-is-error? cb) #f (begin (_emit-byte! cb 255) (cons (_cb->list cb) (_cs->list cs))))))
