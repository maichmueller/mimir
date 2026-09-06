;; A set that is a landmark only through ONE of its members, where no achiever intersection can say so.
;;
;; `need1` is achieved by `mk_need1(?x)` for any `?x`, so the back-chain sees the disjunction
;; `a(?)` = {a(t1), a(t2)} and stops: neither member is individually necessary *for need1*.
;;
;; `need2` is achieved two ways, through `r1` or through `r2`, which share no predicate -- so the
;; §2.4 intersection over its achievers is empty and that chain dies immediately. Both routes
;; nevertheless run through `a(t1)`, one step further down, which is a fact about REACHABILITY and
;; not about any single achiever's preconditions. Only the complete Pi+ test (§9.5) sees it: forbid
;; `a(t1)` and neither `r1` nor `r2` can be derived, so `need2` and the goal become unreachable.
(define (domain landmark-lifted-complete-member)
  (:requirements :strips :typing)
  (:types thing)
  (:constants t1 - thing)
  (:predicates
    (goal) (need1) (need2) (r1) (r2)
    (a ?x - thing)
    (base ?x - thing))

  (:action finish
    :parameters ()
    :precondition (and (need1) (need2))
    :effect (and (goal)))

  (:action mk_a
    :parameters (?x - thing)
    :precondition (and (base ?x))
    :effect (and (a ?x)))

  (:action mk_need1
    :parameters (?x - thing)
    :precondition (and (a ?x))
    :effect (and (need1)))

  (:action mk_need2_via_r1
    :parameters ()
    :precondition (and (r1))
    :effect (and (need2)))

  (:action mk_need2_via_r2
    :parameters ()
    :precondition (and (r2))
    :effect (and (need2)))

  (:action mk_r1
    :parameters ()
    :precondition (and (a t1))
    :effect (and (r1)))

  (:action mk_r2
    :parameters ()
    :precondition (and (a t1))
    :effect (and (r2))))
