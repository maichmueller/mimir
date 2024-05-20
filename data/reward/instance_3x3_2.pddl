
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Instance file automatically generated by the Tarski FSTRIPS writer
;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (problem reward-3x3)
    (:domain reward-strips)
    (:requirements :typing)
    (:objects
        c_0_0 c_0_1 c_0_2 c_1_0 c_1_1 c_1_2 c_2_0 c_2_1 c_2_2 - cell
    )

    (:init
        (adjacent c_2_2 c_1_2)
        (adjacent c_0_1 c_0_2)
        (adjacent c_1_2 c_2_2)
        (adjacent c_1_0 c_0_0)
        (adjacent c_2_0 c_2_1)
        (adjacent c_0_0 c_1_0)
        (adjacent c_0_2 c_1_2)
        (adjacent c_1_0 c_2_0)
        (adjacent c_1_2 c_1_1)
        (adjacent c_0_0 c_0_1)
        (adjacent c_2_1 c_2_0)
        (adjacent c_1_1 c_2_1)
        (adjacent c_2_0 c_1_0)
        (adjacent c_2_1 c_2_2)
        (adjacent c_1_1 c_1_2)
        (adjacent c_0_1 c_1_1)
        (adjacent c_0_2 c_0_1)
        (adjacent c_1_2 c_0_2)
        (adjacent c_1_1 c_1_0)
        (adjacent c_1_1 c_0_1)
        (adjacent c_0_1 c_0_0)
        (adjacent c_1_0 c_1_1)
        (adjacent c_2_1 c_1_1)
        (adjacent c_2_2 c_2_1)
        (at c_0_0)
        (unblocked c_1_2)
        (unblocked c_0_1)
        (unblocked c_2_2)
        (unblocked c_1_1)
        (unblocked c_0_2)
        (unblocked c_2_1)
        (unblocked c_1_0)
        (unblocked c_2_0)
        (unblocked c_0_0)
        (reward c_1_2)
        (reward c_0_1)
        (reward c_2_2)
        (reward c_0_2)
        (reward c_1_0)
        (reward c_2_0)
        (reward c_1_1)
    )

    (:goal
        (and (picked c_0_1) (picked c_2_2) (picked c_1_2) (picked c_0_2) (picked c_2_0) (picked c_1_0) (picked c_1_1))
    )




)

