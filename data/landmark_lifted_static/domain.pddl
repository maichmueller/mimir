;; Two ways to reach the goal, one of them gated by a *static* predicate.
;;
;; `enabled` never appears in an effect, so mimir classifies it as static and the lifted extractor's
;; static filter may drop `achieve-slow` whenever the instance carries no matching `enabled` atom.
;; With the filter on, `achieve-fast` is the only achiever and its preconditions are fact landmarks;
;; with it off, the intersection has to hold over an achiever that can never fire, which turns
;; `ready` partial and loses `p` entirely.
(define (domain landmark-lifted-static)
  (:requirements :strips :typing)
  (:types thing)
  (:predicates
    (goal ?x - thing)
    (p ?x - thing)
    (ready ?x - thing)
    (enabled ?x - thing))

  (:action achieve-fast
    :parameters (?x - thing)
    :precondition (and (p ?x) (ready ?x))
    :effect (and (goal ?x)))

  (:action achieve-slow
    :parameters (?x - thing ?y - thing)
    :precondition (and (enabled ?x) (ready ?y))
    :effect (and (goal ?x)))

  (:action make-ready
    :parameters (?x - thing)
    :precondition (and)
    :effect (and (ready ?x)))

  (:action make-p
    :parameters (?x - thing)
    :precondition (and)
    :effect (and (p ?x))))
