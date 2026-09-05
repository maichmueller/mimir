;; No `held` atom in `I`: the gate is open and §2.7 narrows `ready(?)` to the things that start ready.
(define (problem landmark-lifted-sdp-gate-open)
  (:domain landmark-lifted-sdp-gate)
  (:objects t1 t2 t3 - thing)
  (:init (usable t1) (usable t3) (ready t1) (ready t3))
  (:goal (and (goal))))
