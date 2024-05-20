
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Instance file automatically generated by the Tarski FSTRIPS writer
;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (problem reward-2x2)
    (:domain reward-strips)
    (:requirements :typing)
    (:objects
        c_0_0 c_0_1 c_1_0 c_1_1 - cell
    )

    (:init
        (adjacent c_0_0 c_1_0)
        (adjacent c_1_1 c_1_0)
        (adjacent c_1_1 c_0_1)
        (adjacent c_0_1 c_0_0)
        (adjacent c_1_0 c_0_0)
        (adjacent c_0_0 c_0_1)
        (adjacent c_0_1 c_1_1)
        (adjacent c_1_0 c_1_1)
        (at c_0_0)
        (unblocked c_1_1)
        (unblocked c_1_0)
        (unblocked c_0_0)
        (reward c_1_0)
    )

    (:goal
        (picked c_1_0)
    )




)

