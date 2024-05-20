;; blocks=4, percentage_new_tower=20, out_folder=., instance_id=437, seed=17

(define (problem blocksworld-437)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 - object)
 (:init
    (clear b3)
    (on b3 b4)
    (on b4 b2)
    (on-table b2)
    (clear b1)
    (on-table b1))
 (:goal  (and
    (clear b3)
    (on-table b3)
    (clear b4)
    (on-table b4)
    (clear b1)
    (on b1 b2)
    (on-table b2))))
