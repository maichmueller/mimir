;; blocks=5, percentage_new_tower=40, out_folder=., instance_id=579, seed=9

(define (problem blocksworld-579)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 b5 - object)
 (:init
    (clear b2)
    (on-table b2)
    (clear b4)
    (on b4 b1)
    (on b1 b5)
    (on b5 b3)
    (on-table b3))
 (:goal  (and
    (clear b5)
    (on b5 b2)
    (on b2 b4)
    (on-table b4)
    (clear b3)
    (on-table b3)
    (clear b1)
    (on-table b1))))
