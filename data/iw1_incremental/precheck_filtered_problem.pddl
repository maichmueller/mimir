(define (problem iw1_precheck_filtered)
 (:domain iw1_incremental)
 (:objects a - item)
 (:init
    (mode-precheck-branch)
    (used a))
 (:goal
    (and
        (branch-left))))
