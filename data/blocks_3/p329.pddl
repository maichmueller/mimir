;; blocks=3, percentage_new_tower=20, out_folder=., instance_id=329, seed=29

(define (problem blocksworld-329)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 b3 - object)
 (:init
    (clear b3)
    (on b3 b1)
    (on b1 b2)
    (on-table b2))
 (:goal  (and
    (clear b3)
    (on b3 b1)
    (on b1 b2)
    (on-table b2))))
