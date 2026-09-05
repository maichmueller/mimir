;; A self-dependent precondition behind a gate that a non-member initial atom can close.
;;
;; `held` is achieved only by `grab`, whose `ready` precondition is added only by `prep`, which
;; needs `held` itself -- so §2.7 can conclude that the `ready` instance the first `grab` uses is an
;; initial one, and narrow `?x` to the things that start ready.
;;
;; It may conclude that only while NO instance of the `held(?)` pattern is true in `I`. `usable` is
;; static and lets a problem put a `held` atom in `I` that is not a member of the recorded landmark
;; -- harmless for §2.1's member-level stop, fatal for §2.7's argument, which is exactly the case
;; the gate exists for.
(define (domain landmark-lifted-sdp-gate)
  (:requirements :strips :typing)
  (:types thing)
  (:predicates
    (goal)
    (held ?x - thing)
    (ready ?x - thing)
    (usable ?x - thing))

  (:action finish
    :parameters (?x - thing)
    :precondition (and (usable ?x) (held ?x))
    :effect (and (goal)))

  (:action grab
    :parameters (?x - thing)
    :precondition (and (ready ?x))
    :effect (and (held ?x)))

  (:action prep
    :parameters (?x - thing)
    :precondition (and (held ?x))
    :effect (and (ready ?x))))
