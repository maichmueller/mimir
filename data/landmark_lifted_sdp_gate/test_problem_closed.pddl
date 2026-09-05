;; `held(t2)` is in `I` and is NOT a member of the recorded landmark -- t2 is not `usable`, so no
;; `finish` can use it. The gate closes and the expansion proceeds plainly.
(define (problem landmark-lifted-sdp-gate-closed)
  (:domain landmark-lifted-sdp-gate)
  (:objects t1 t2 t3 - thing)
  (:init (usable t1) (usable t3) (ready t1) (ready t3) (held t2))
  (:goal (and (goal))))
