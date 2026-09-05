;; A predicate whose adder is gated by a static condition only some objects satisfy.
;;
;; `marked` is added only by `mark`, whose `markable` precondition is static, so `marked(t3)` can
;; never be true in any reachable state -- and a member set containing it would be claiming that a
;; plan might discharge the obligation by making it true. `need` is the control: `want` adds it for
;; anything, so every instance stays a member.
(define (domain landmark-lifted-unreachable-members)
  (:requirements :strips :typing)
  (:types thing)
  (:predicates
    (goal)
    (need ?x - thing)
    (marked ?x - thing)
    (markable ?x - thing))

  (:action finish
    :parameters (?x - thing)
    :precondition (and (need ?x) (marked ?x))
    :effect (and (goal)))

  (:action mark
    :parameters (?x - thing)
    :precondition (and (markable ?x))
    :effect (and (marked ?x)))

  (:action want
    :parameters (?x - thing)
    :precondition (and)
    :effect (and (need ?x))))
