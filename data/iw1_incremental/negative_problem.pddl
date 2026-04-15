(define (problem iw1_negative)
 (:domain iw1_incremental)
 (:objects a - item)
 (:init
    (mode-neg)
    (present a))
 (:goal
    (and
        (goal-neg))))
