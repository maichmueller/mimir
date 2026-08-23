(define (problem iw1_ever_tested)
 (:domain iw1_incremental)
 (:objects a - item)
 (:init
    (mode-ever))
 (:goal
    (and
        (used a))))
