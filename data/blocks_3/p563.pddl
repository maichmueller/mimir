;; blocks=5, percentage_new_tower=20, out_folder=., instance_id=563, seed=23

(define (problem blocksworld-563)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 b5 - object)
 (:init
    (clear b1)
    (on b1 b3)
    (on b3 b4)
    (on b4 b2)
    (on b2 b5)
    (on-table b5))
 (:goal  (and
    (clear b2)
    (on b2 b3)
    (on b3 b4)
    (on-table b4)
    (clear b1)
    (on b1 b5)
    (on-table b5))))
