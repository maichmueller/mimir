;; `t3` is not `markable`, so `marked(t3)` is unreachable and must not be a member.
(define (problem landmark-lifted-unreachable-members-1)
  (:domain landmark-lifted-unreachable-members)
  (:objects t1 t2 t3 - thing)
  (:init (markable t1) (markable t2))
  (:goal (and (goal))))
