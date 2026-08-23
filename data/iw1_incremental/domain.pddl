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
    (mode-precheck-branch)
    (mode-precheck-repeat)
    (branch-left)
    (branch-right)
    (cycle-ready)
    (cycle-two)
    (cycle-reenabled)
    (enabled ?x - thing)
    (prechecked ?x - thing)
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
    :effect (and (used ?x)))

 (:action branch-left-precheck
    :parameters (?x - item)
    :precondition (and (mode-precheck-branch))
    :effect (and (prechecked ?x)
                 (branch-left)
                 (not (mode-precheck-branch))))

 (:action branch-right-precheck
    :parameters (?x - item)
    :precondition (and (mode-precheck-branch))
    :effect (and (prechecked ?x)
                 (branch-right)
                 (not (mode-precheck-branch))))

 (:action use-prechecked
    :parameters (?x - item)
    :precondition (and (prechecked ?x))
    :effect (and (used ?x)))

 (:action precheck-enable
    :parameters (?x - item)
    :precondition (and (mode-precheck-repeat))
    :effect (and (enabled ?x)
                 (cycle-ready)
                 (not (mode-precheck-repeat))))

 (:action disable-enabled
    :parameters (?x - item)
    :precondition (and (enabled ?x)
                       (cycle-ready)
                       (not (cycle-reenabled)))
    :effect (and (not (enabled ?x))
                 (cycle-two)))

 (:action reenable
    :parameters (?x - item)
    :precondition (and (cycle-two))
    :effect (and (enabled ?x)
                 (cycle-reenabled)
                 (not (cycle-two)))))
