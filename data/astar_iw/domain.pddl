(define (domain astar-iw-test)
  (:requirements :strips :negative-preconditions)
  (:predicates (seed) (a) (b) (goal))

  (:action add-a
    :parameters ()
    :precondition (seed)
    :effect (a))

  (:action add-b
    :parameters ()
    :precondition (seed)
    :effect (b))

  (:action finish
    :parameters ()
    :precondition (and (a) (b))
    :effect (goal))

  (:action clear-seed
    :parameters ()
    :precondition (seed)
    :effect (not (seed)))
)
