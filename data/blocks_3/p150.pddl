;; blocks=2, percentage_new_tower=10, out_folder=., instance_id=150, seed=0

(define (problem blocksworld-150)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 - object)
 (:init
    (clear b1)
    (on b1 b2)
    (on-table b2))
 (:goal  (and
    (clear b1)
    (on-table b1)
    (clear b2)
    (on-table b2))))
