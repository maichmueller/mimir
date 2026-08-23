;; Hand-built STRIPS fixture for landmark-informed transition ordering.
;;
;; From the initial state both `del-landmark` and `del-nonlandmark` are applicable and both add
;; exactly the same single new fluent atom (w). Under IW(1) novelty they therefore compete for the
;; *same* novelty witness: whichever transition is admitted first claims (w) and the other one is
;; left with an empty novel-atom delta.
;;
;; `del-landmark` is declared first, so the default (queued) generation order admits it -- and it
;; deletes the landmark (p), after which the goal is unreachable. The landmark ordering prefers
;; `del-nonlandmark` (it deletes zero achieved landmarks), which keeps (p) and lets `finish` fire.
(define (domain landmark-transition-ordering)
  (:requirements :strips)
  (:predicates (r) (p) (q) (w) (g) (enabled))

  ;; Deletes the landmark (p). Enumerated first on purpose.
  (:action del-landmark
    :parameters ()
    :precondition (r)
    :effect (and (w) (not (p))))

  ;; Deletes the non-landmark (q).
  (:action del-nonlandmark
    :parameters ()
    :precondition (r)
    :effect (and (w) (not (q))))

  ;; Unique achiever of the goal landmark (g); its preconditions (w) and (p) are the landmarks.
  (:action finish
    :parameters ()
    :precondition (and (w) (p))
    :effect (g))

  ;; Never applicable: (enabled) is static (it appears in no effect) and absent from the initial
  ;; state. This action exists only so that (p) and (q) appear in an *add* effect somewhere in the
  ;; domain and are therefore classified as fluent rather than static predicates.
  (:action reset
    :parameters ()
    :precondition (enabled)
    :effect (and (p) (q))))
