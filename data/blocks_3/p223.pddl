;; blocks=2, percentage_new_tower=40, out_folder=., instance_id=223, seed=13

(define (problem blocksworld-223)
 (:domain blocksworld)
(:requirements :typing)
 (:objects b1 b2 - object)
 (:init
    (clear b1)
    (on b1 b2)
    (on-table b2))
 (:goal  (and
    (clear b1)
    (on b1 b2)
    (on-table b2))))
