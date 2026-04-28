; src/scheme/lib.scm — bootstrap library for list and vector utilities
; Loaded during Phase 1 after compiler.scm (C compiler constraints apply)
;
; NOTE: Avoid `let` in this file — the C compiler's closure capture for
; nested lambdas has a GC issue that causes crashes when the resulting
; functions are called from Scheme-compiled code. Use top-level helpers
; with explicit parameter passing instead.

; --- List predicate ---

(define list?
  (lambda (x)
    (if (null? x)
        #t
        (if (pair? x)
            (list? (cdr x))
            #f))))

; --- List operations ---

(define list-copy
  (lambda (x)
    (if (null? x)
        '()
        (cons (car x) (list-copy (cdr x))))))

(define _list-tail
  (lambda (x k)
    (if (= k 0)
        x
        (_list-tail (cdr x) (- k 1)))))

(define list-tail
  (lambda (x k)
    (_list-tail x k)))

(define list-ref
  (lambda (x k)
    (car (list-tail x k))))

(define list-set!
  (lambda (x k v)
    (set-car! (list-tail x k) v)))

; --- List search ---

(define memq
  (lambda (x ls)
    (if (null? ls)
        #f
        (if (eq? x (car ls))
            ls
            (memq x (cdr ls))))))

(define memv
  (lambda (x ls)
    (if (null? ls)
        #f
        (if (eqv? x (car ls))
            ls
            (memv x (cdr ls))))))

(define member
  (lambda (x ls)
    (if (null? ls)
        #f
        (if (equal? x (car ls))
            ls
            (member x (cdr ls))))))

; --- Association lists ---

(define assq
  (lambda (x alist)
    (if (null? alist)
        #f
        (if (eq? x (car (car alist)))
            (car alist)
            (assq x (cdr alist))))))

(define assv
  (lambda (x alist)
    (if (null? alist)
        #f
        (if (eqv? x (car (car alist)))
            (car alist)
            (assv x (cdr alist))))))

(define assoc
  (lambda (x alist)
    (if (null? alist)
        #f
        (if (equal? x (car (car alist)))
            (car alist)
            (assoc x (cdr alist))))))

; --- Iteration ---

(define map
  (lambda (proc ls)
    (if (null? ls)
        '()
        (cons (proc (car ls)) (map proc (cdr ls))))))

(define for-each
  (lambda (proc ls)
    (if (null? ls)
        #f
        (begin
          (proc (car ls))
          (for-each proc (cdr ls))))))

(define filter
  (lambda (pred ls)
    (if (null? ls)
        '()
        (if (pred (car ls))
            (cons (car ls) (filter pred (cdr ls)))
            (filter pred (cdr ls))))))

; --- Vector iteration helpers ---

(define _vfi
  (lambda (vec fill i)
    (if (< i (vector-length vec))
        (begin
          (vector-set! vec i fill)
          (_vfi vec fill (+ i 1)))
        #f)))

(define _vcp
  (lambda (src dst i)
    (if (< i (vector-length src))
        (begin
          (vector-set! dst i (vector-ref src i))
          (_vcp src dst (+ i 1)))
        dst)))

(define _vap-copy
  (lambda (src dst offset i)
    (if (< i (vector-length src))
        (begin
          (vector-set! dst (+ offset i) (vector-ref src i))
          (_vap-copy src dst offset (+ i 1)))
        dst)))

(define _vap-inner
  (lambda (a b lena lenb result)
    (_vap-copy a result 0 0)
    (_vap-copy b result lena 0)
    result))

(define _vmap
  (lambda (proc src dst i)
    (if (< i (vector-length src))
        (begin
          (vector-set! dst i (proc (vector-ref src i)))
          (_vmap proc src dst (+ i 1)))
        dst)))

; --- Vector utilities ---

(define vector-fill!
  (lambda (vec fill)
    (_vfi vec fill 0)))

(define vector-copy
  (lambda (vec)
    (_vcp vec (make-vector (vector-length vec) #f) 0)))

(define vector-append
  (lambda (a b)
    (_vap-inner a b
                (vector-length a) (vector-length b)
                (make-vector (+ (vector-length a) (vector-length b)) #f))))

(define vector-map
  (lambda (proc vec)
    (_vmap proc vec (make-vector (vector-length vec) #f) 0)))

; ==================== Character predicates ====================

(define char-alphabetic?
  (lambda (c)
    (if (< (char->integer c) 65)
        #f
        (if (> (char->integer c) 90)
            (if (< (char->integer c) 97)
                #f
                (if (> (char->integer c) 122) #f #t))
            #t))))

(define char-numeric?
  (lambda (c)
    (if (< (char->integer c) 48)
        #f
        (if (> (char->integer c) 57) #f #t))))

(define char-whitespace?
  (lambda (c)
    (if (char=? c #\space)
        #t
        (if (char=? c #\newline)
            #t
            (if (char=? c #\tab)
                #t
                (char=? c #\return))))))

(define char-upper?
  (lambda (c)
    (if (< (char->integer c) 65)
        #f
        (if (> (char->integer c) 90) #f #t))))

(define char-lower?
  (lambda (c)
    (if (< (char->integer c) 97)
        #f
        (if (> (char->integer c) 122) #f #t))))

(define char-digit?
  (lambda (c)
    (if (< (char->integer c) 48)
        #f
        (if (> (char->integer c) 57) #f #t))))

; ==================== Case conversion ====================

(define char-upcase
  (lambda (c)
    (if (< (char->integer c) 97)
        c
        (if (> (char->integer c) 122)
            c
            (integer->char (- (char->integer c) 32))))))

(define char-downcase
  (lambda (c)
    (if (< (char->integer c) 65)
        c
        (if (> (char->integer c) 90)
            c
            (integer->char (+ (char->integer c) 32))))))

; ==================== Case-insensitive comparison ====================

(define char-ci=?
  (lambda (a b) (char=? (char-downcase a) (char-downcase b))))

(define char-ci<?
  (lambda (a b) (char<? (char-downcase a) (char-downcase b))))

(define char-ci>?
  (lambda (a b) (char>? (char-downcase a) (char-downcase b))))

(define char-ci<=?
  (lambda (a b) (char<=? (char-downcase a) (char-downcase b))))

(define char-ci>=?
  (lambda (a b) (char>=? (char-downcase a) (char-downcase b))))

; ==================== String library ====================

(define _str-cpy
  (lambda (src dst i)
    (if (< i (string-length src))
        (begin
          (string-set! dst i (string-ref src i))
          (_str-cpy src dst (+ i 1)))
        dst)))

(define string-copy
  (lambda (s)
    (_str-cpy s (make-string (string-length s) #\space) 0)))

(define _str-cpy-at
  (lambda (src dst si di)
    (if (< si (string-length src))
        (begin
          (string-set! dst di (string-ref src si))
          (_str-cpy-at src dst (+ si 1) (+ di 1)))
        dst)))

(define _str-app
  (lambda (a b la lb result)
    (_str-cpy-at a result 0 0)
    (_str-cpy-at b result 0 la)
    result))

(define string-append
  (lambda (a b)
    (_str-app a b (string-length a) (string-length b)
              (make-string (+ (string-length a) (string-length b)) #\space))))

(define _str-map
  (lambda (proc src dst i)
    (if (< i (string-length src))
        (begin
          (string-set! dst i (proc (string-ref src i)))
          (_str-map proc src dst (+ i 1)))
        dst)))

(define string-upcase
  (lambda (s)
    (_str-map char-upcase s (make-string (string-length s) #\space) 0)))

(define string-downcase
  (lambda (s)
    (_str-map char-downcase s (make-string (string-length s) #\space) 0)))

; ==================== Bytevector library ====================

(define _bv-cpy
  (lambda (src dst i)
    (if (< i (bytevector-length src))
        (begin
          (bytevector-u8-set! dst i (bytevector-u8-ref src i))
          (_bv-cpy src dst (+ i 1)))
        dst)))

(define bytevector-copy
  (lambda (bv)
    (_bv-cpy bv (make-bytevector (bytevector-length bv)) 0)))

(define _bv-cpy-at
  (lambda (src dst si di)
    (if (< si (bytevector-length src))
        (begin
          (bytevector-u8-set! dst di (bytevector-u8-ref src si))
          (_bv-cpy-at src dst (+ si 1) (+ di 1)))
        dst)))

(define _bv-app
  (lambda (a b la lb result)
    (_bv-cpy-at a result 0 0)
    (_bv-cpy-at b result 0 la)
    result))

(define bytevector-append
  (lambda (a b)
    (_bv-app a b (bytevector-length a) (bytevector-length b)
             (make-bytevector (+ (bytevector-length a) (bytevector-length b)) 0))))

;; ============================================================
;; Macro system — *macro-table*, syntax-rules, pattern matching
;; ============================================================

;; Mutable cell: car holds the macro alist
(define *macro-table* (cons '() '()))

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

;; Simplified non-hygienic version (string-append/string->symbol not available)
(define (_rename-sym sym rename-id)
  sym)

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

(define _macro-id-cell (cons 0 '()))

;; syntax-rules: simplified closure construction using an explicit helper
(define (_make-transformer-apply form macro-id clauses literals call-id-cell)
  (_try-clauses form clauses literals (+ (* macro-id 1000) (car call-id-cell))))

(define (_make-transformer macro-id clauses literals call-id-cell)
  (lambda (form)
    (_make-transformer-apply form macro-id clauses literals call-id-cell)))

(define (syntax-rules literals clauses)
  (begin
    (set-car! _macro-id-cell (+ (car _macro-id-cell) 1))
    (_make-transformer (- (car _macro-id-cell) 1) clauses literals (cons 0 '()))))

(define (_try-clauses form clauses literals rename-id)
  form)
