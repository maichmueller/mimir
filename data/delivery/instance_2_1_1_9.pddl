
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Instance file automatically generated by the Tarski FSTRIPS writer
;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (problem delivery-2x1-1)
    (:domain delivery)
    (:requirements :equality :typing)
    (:objects
        c_0_0 c_1_0 - cell
        p1 - package
        t1 - truck
    )

    (:init
        (adjacent c_1_0 c_0_0)
        (adjacent c_0_0 c_1_0)
        (at p1 c_1_0)
        (at t1 c_1_0)
        (empty t1)
    )

    (:goal
        (at p1 c_1_0)
    )




)

