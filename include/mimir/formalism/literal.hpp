/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MIMIR_FORMALISM_LITERAL_HPP_
#define MIMIR_FORMALISM_LITERAL_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/equal_to.hpp"
#include "mimir/formalism/hash.hpp"

namespace mimir
{
/*
    TODO: Flattening LiteralImpl is unnecessary. It is already flat.
*/
template<PredicateCategory P>
class LiteralImpl
{
private:
    size_t m_index;
    bool m_is_negated;
    Atom<P> m_atom;

    // Below: add additional members if needed and initialize them in the constructor

    LiteralImpl(size_t index, bool is_negated, Atom<P> atom);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    using Category = P;

    size_t get_index() const;
    bool is_negated() const;
    const Atom<P>& get_atom() const;
};

template<PredicateCategory P>
struct UniquePDDLHasher<const LiteralImpl<P>*>
{
    size_t operator()(const LiteralImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniquePDDLEqualTo<const LiteralImpl<P>*>
{
    bool operator()(const LiteralImpl<P>* l, const LiteralImpl<P>* r) const;
};

template<PredicateCategory P>
extern std::ostream& operator<<(std::ostream& out, const LiteralImpl<P>& element);

}

#endif
