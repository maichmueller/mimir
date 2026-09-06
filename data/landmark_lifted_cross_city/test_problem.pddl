;; Two cities, one truck each, one airplane, one package to move across.
;;
;; A truck can only DRIVE-TRUCK within its own city (`in-city` is static and appears twice in the
;; precondition), and nothing ever changes which city a location is in -- but which city a *truck*
;; starts in is a FLUENT initial atom, `(at t0 l0-0)`, so no static analysis can tell that `t0` will
;; never be at `l1-1`. The lifted rule therefore proposes `at(?, l1-1)` over every vehicle, and only
;; delete-relaxed reachability (§9.1) can cut it down to the ones that can actually get there.
(define (problem landmark-lifted-cross-city)
  (:domain logistics-strips)
  (:objects a0
            c0 c1
            t0 t1
            l0-0 l0-1
            l1-0 l1-1
            p0
  )
  (:init
    (AIRPLANE a0)
    (CITY c0)
    (CITY c1)
    (TRUCK t0)
    (TRUCK t1)
    (LOCATION l0-0)
    (LOCATION l0-1)
    (LOCATION l1-0)
    (LOCATION l1-1)
    (in-city l0-0 c0)
    (in-city l0-1 c0)
    (in-city l1-0 c1)
    (in-city l1-1 c1)
    (AIRPORT l0-0)
    (AIRPORT l1-0)
    (OBJ p0)
    (at t0 l0-0)
    (at t1 l1-0)
    (at a0 l0-0)
    (at p0 l0-1)
  )
  (:goal
    (and (at p0 l1-1))
  )
)
