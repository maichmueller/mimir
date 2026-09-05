;; Same as `test_problem.pddl`, plus `(ready t2)` in the initial state.
;;
;; With the static filter off, `ready(?)` is derived with `ready(t1)` as its only member -- the
;; `achieve-slow` completions are statically inconsistent and contribute nothing. `(ready t2)` is an
;; instance of the *pattern* that holds at `I` but is not a member, so it is no way to satisfy the
;; recorded landmark and must not stop the back-chain.
(define (problem landmark-lifted-static-2)
  (:domain landmark-lifted-static)
  (:objects t1 t2 - thing)
  (:init (ready t2))
  (:goal (and (goal t1))))
