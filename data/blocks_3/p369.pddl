;; blocks=4, percentage_new_tower=0, out_folder=., instance_id=369, seed=9

(define (problem blocksworld-369)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 - object)
 (:init
    (clear b2)
    (on b2 b1)
    (on b1 b4)
    (on b4 b3)
    (on-table b3))
 (:goal  (and
    (clear b4)
    (on b4 b1)
    (on b1 b3)
    (on b3 b2)
    (on-table b2))))
