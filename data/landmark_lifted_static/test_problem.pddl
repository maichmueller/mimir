;; No `enabled` atom at all: `achieve-slow` has no statically consistent ground instance.
(define (problem landmark-lifted-static-1)
  (:domain landmark-lifted-static)
  (:objects t1 t2 - thing)
  (:init)
  (:goal (and (goal t1))))
