(define (domain iw1_incremental)
 (:requirements :typing :strips :negative-preconditions)
 (:types
    thing - object
    item special-item - thing)
 (:constants hub - special-item)
 (:predicates
    (mode-pos)
    (mode-neg)
    (mode-same)
    (mode-const)
    (mode-ever)
    (branch-left)
    (branch-right)
    (enabled ?x - thing)
    (present ?x - thing)
    (diag ?x - thing ?y - thing)
    (goal-pos)
    (goal-neg)
    (goal-same)
    (goal-const)
    (after-neg)
    (used ?x - thing)
    (dummy-goal))

 (:action enable
    :parameters (?x - item)
    :precondition (and (mode-pos))
    :effect (and (enabled ?x)
                 (not (mode-pos))))

 (:action use-enabled
    :parameters (?x - item)
    :precondition (and (enabled ?x))
    :effect (and (goal-pos)))

 (:action remove-present
    :parameters (?x - item)
    :precondition (and (mode-neg)
                       (present ?x))
    :effect (and (not (present ?x))
                 (after-neg)
                 (not (mode-neg))))

 (:action use-not-present
    :parameters (?x - item)
    :precondition (and (not (present ?x)))
    :effect (and (goal-neg)))

 (:action activate-diag
    :parameters (?x - item)
    :precondition (and (mode-same))
    :effect (and (diag ?x ?x)
                 (not (mode-same))))

 (:action use-diag
    :parameters (?x - item)
    :precondition (and (diag ?x ?x))
    :effect (and (goal-same)))

 (:action open-hub
    :parameters ()
    :precondition (and (mode-const))
    :effect (and (enabled hub)
                 (not (mode-const))))

 (:action use-hub
    :parameters ()
    :precondition (and (enabled hub))
    :effect (and (goal-const)))

 (:action branch-left-enable
    :parameters (?x - item)
    :precondition (and (mode-ever))
    :effect (and (enabled ?x)
                 (branch-left)
                 (not (mode-ever))))

 (:action branch-right-enable
    :parameters (?x - item)
    :precondition (and (mode-ever))
    :effect (and (enabled ?x)
                 (branch-right)
                 (not (mode-ever))))

 (:action use-shared
    :parameters (?x - item)
    :precondition (and (enabled ?x))
    :effect (and (used ?x))))
