;; blocks=1, percentage_new_tower=20, out_folder=., instance_id=87, seed=27

(define (problem blocksworld-87)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 - object)
 (:init
    (clear b1)
    (on-table b1))
 (:goal  (and
    (clear b1)
    (on-table b1))))
