;; One achiever mentions the same predicate twice, the other once.
;;
;; Footnote 1 of Wichlacz et al. (IJCAI 2022): each way of picking one occurrence per achiever is a
;; landmark of its own and they differ. Here `act-two` contributes `(q a ?x)` and `(q ?y b)` while
;; `act-one` contributes `(q a b)`, so the two choice vectors agree on the first position in one case
;; and on the second in the other, yielding `q(a, ?)` and `q(?, b)`.
(define (domain landmark-lifted-occurrences)
  (:requirements :strips :typing)
  (:types obj)
  (:constants a b - obj)
  (:predicates
    (goal)
    (q ?x - obj ?y - obj))

  (:action act-two
    :parameters (?x - obj ?y - obj)
    :precondition (and (q a ?x) (q ?y b))
    :effect (and (goal)))

  (:action act-one
    :parameters ()
    :precondition (and (q a b))
    :effect (and (goal)))

  (:action make-q
    :parameters (?x - obj ?y - obj)
    :precondition (and)
    :effect (and (q ?x ?y))))
