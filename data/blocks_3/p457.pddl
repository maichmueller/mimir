;; blocks=4, percentage_new_tower=40, out_folder=., instance_id=457, seed=7

(define (problem blocksworld-457)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 b4 - object)
 (:init
    (clear b4)
    (on b4 b1)
    (on-table b1)
    (clear b3)
    (on b3 b2)
    (on-table b2))
 (:goal  (and
    (clear b4)
    (on b4 b1)
    (on b1 b2)
    (on b2 b3)
    (on-table b3))))