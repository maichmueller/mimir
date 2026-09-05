;; The one place where this engine and `LiftedGrounder` genuinely disagree.
;;
;; `blocked` is static -- no effect ever touches it -- so `(not (blocked ?i))` is exactly evaluable in the
;; delete relaxation: deleting fluent effects cannot make a static atom go away. mimir's
;; `DeleteRelaxTranslator` nevertheless drops every negative literal regardless of tag
;; (`filter_positive_literals`), so its delete-free exploration instantiates `step1(b)` and reaches `mid(b)`
;; and `goal(b)`. `create_ground_actions()` then filters `step1(b)` back out on static applicability, but
;; `step2(b)` survives the filter -- it has no static condition of its own -- so the returned ground-action
;; universe is a strict superset of the reachable one.
(define (domain relaxed-reachability-negative-static)
  (:requirements :strips :typing :negative-preconditions)
  (:types item)
  (:predicates
    (start ?i - item)
    (blocked ?i - item)
    (mid ?i - item)
    (goal ?i - item))

  (:action step1
    :parameters (?i - item)
    :precondition (and (start ?i) (not (blocked ?i)))
    :effect (mid ?i))

  (:action step2
    :parameters (?i - item)
    :precondition (mid ?i)
    :effect (goal ?i))
)
