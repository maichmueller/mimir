;; Hand-built STRIPS fixture for cross-parent admission of a shared successor state.
;;
;; The initial state expands into two distinct depth-1 states, P1 = {p, u1} (via `to-p1`) and
;; P2 = {u2} (via `to-p2`). Both of them reach the *same* depth-2 state S = {t}, through
;; `join-from-p1` and `join-from-p2` respectively.
;;
;; `join-from-p1` deletes the landmark (p) on the way, `join-from-p2` deletes nothing that is a
;; landmark, so the landmark ordering prefers `join-from-p2`. Under duplicate pruning only the
;; first-admitted transition enters the search tree, so the recorded parent/action of S differs
;; between the default queued order (P1 / join-from-p1, since P1 is expanded first) and the
;; landmark-ordered deferred admission pass (P2 / join-from-p2).
(define (domain landmark-cross-parent)
  (:requirements :strips)
  (:predicates (r) (p) (u1) (u2) (t) (g) (enabled))

  (:action to-p1
    :parameters ()
    :precondition (r)
    :effect (and (u1) (not (r))))

  (:action to-p2
    :parameters ()
    :precondition (r)
    :effect (and (u2) (not (r)) (not (p))))

  ;; Reaches S = {t} from P1 = {p, u1}, deleting the landmark (p).
  (:action join-from-p1
    :parameters ()
    :precondition (u1)
    :effect (and (t) (not (u1)) (not (p))))

  ;; Reaches the same S = {t} from P2 = {u2}, deleting no landmark.
  (:action join-from-p2
    :parameters ()
    :precondition (u2)
    :effect (and (t) (not (u2))))

  ;; Unique achiever of the goal landmark (g): makes both (t) and (p) landmarks.
  (:action finish
    :parameters ()
    :precondition (and (t) (p))
    :effect (g))

  ;; Never applicable ((enabled) is static and false); present only so that (r), (p), (u1) and (u2)
  ;; all appear in an add effect and are classified as fluent rather than static predicates.
  (:action reset
    :parameters ()
    :precondition (enabled)
    :effect (and (r) (p) (u1) (u2))))
