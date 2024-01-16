#ifndef MIMIR_FORMALISM_ATOM_HPP_
#define MIMIR_FORMALISM_ATOM_HPP_

#include "predicate.hpp"
#include "term.hpp"

#include "../common/mixins.hpp"

#include <loki/domain/pddl/atom.hpp>

#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace mimir
{
    class Atom : public FormattingMixin<Atom>
    {
      private:
        loki::pddl::Atom external_;

        explicit Atom(loki::pddl::Atom external_atom);

      public:
        uint32_t get_id() const;
        Predicate get_predicate() const;
        TermList get_terms() const;

        bool matches(const Atom& first_atom, const Atom& second_atom) const;

        std::size_t hash() const;

        bool operator<(const Atom& other) const;
        bool operator>(const Atom& other) const;
        bool operator==(const Atom& other) const;
        bool operator!=(const Atom& other) const;
        bool operator<=(const Atom& other) const;

        friend class Literal;
        friend class Problem;
    };

    using AtomList = std::vector<Atom>;
    using AtomSet = std::unordered_set<Atom>;

}  // namespace mimir

namespace std
{
    // Inject comparison and hash functions to make pointers behave appropriately with ordered and unordered datastructures
    template<>
    struct hash<mimir::Atom>
    {
        std::size_t operator()(const mimir::Atom& atom) const;
    };

    template<>
    struct hash<mimir::AtomList>
    {
        std::size_t operator()(const mimir::AtomList& atoms) const;
    };

    template<>
    struct less<mimir::Atom>
    {
        bool operator()(const mimir::Atom& left_atom, const mimir::Atom& right_atom) const;
    };

    template<>
    struct equal_to<mimir::Atom>
    {
        bool operator()(const mimir::Atom& left_atom, const mimir::Atom& right_atom) const;
    };
}  // namespace std

#endif  // MIMIR_FORMALISM_ATOM_HPP_
