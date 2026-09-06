;; The five-step cross-city chain §9.3 exists to close.
;;
;; `p0` starts at `l2-1` in city 2 and must reach `l1-3` in city 1. The only route is
;;   at(p0,l2-1) -> in(p0,t2) -> at(p0,l2-0) -> in(p0,a) -> at(p0,l1-0) -> in(p0,t1) -> at(p0,l1-3)
;; and every step of it is forced: one truck per city, one airplane, one airport per city.
;;
;; §2.7 cannot see this. Expanding `in(p0, ?)` it asks whether every adder of `at(p0, ?loc)` needs
;; an instance of the PATTERN `in(p0, ?)` -- it does -- and then requires the `at(p0, ?loc)` the
;; first achiever uses to be initial, which fixes only the first step. The steps after it are about
;; the first achiever of a MEMBER, and only a fixpoint that never derives a member (`R_{¬M}`) can
;; say that a package which has never been in a vehicle cannot have left `l2-1`.
(define (problem landmark-lifted-first-achiever-chain)
  (:domain logistics-strips)
  (:objects a
            c1 c2
            t1 t2
            l1-0 l1-1 l1-2 l1-3
            l2-0 l2-1
            p0
  )
  (:init
    (AIRPLANE a)
    (CITY c1)
    (CITY c2)
    (TRUCK t1)
    (TRUCK t2)
    (LOCATION l1-0)
    (LOCATION l1-1)
    (LOCATION l1-2)
    (LOCATION l1-3)
    (LOCATION l2-0)
    (LOCATION l2-1)
    (in-city l1-0 c1)
    (in-city l1-1 c1)
    (in-city l1-2 c1)
    (in-city l1-3 c1)
    (in-city l2-0 c2)
    (in-city l2-1 c2)
    (AIRPORT l1-0)
    (AIRPORT l2-0)
    (OBJ p0)
    (at t1 l1-1)
    (at t2 l2-1)
    (at a l2-0)
    (at p0 l2-1)
  )
  (:goal
    (and (at p0 l1-3))
  )
)
