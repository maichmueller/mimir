;; blocks=5, percentage_new_tower=20, out_folder=., instance_id=551, seed=11

(define (problem blocksworld-551)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 b5 - object)
 (:init
    (clear b2)
    (on b2 b3)
    (on b3 b1)
    (on b1 b4)
    (on b4 b5)
    (on-table b5))
 (:goal  (and
    (clear b3)
    (on b3 b4)
    (on b4 b2)
    (on b2 b5)
    (on-table b5)
    (clear b1)
    (on-table b1))))
