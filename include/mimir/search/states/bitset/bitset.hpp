#ifndef MIMIR_SEARCH_STATES_BITSET_BITSET_HPP_
#define MIMIR_SEARCH_STATES_BITSET_BITSET_HPP_

#include "../../../formalism/problem/ground_atom.hpp"
#include "../../../formalism/problem/ground_literal.hpp"
#include "interface.hpp"

namespace mimir
{
/**
 * Types
 */
using BitsetStateLayout = flatmemory::Tuple<uint32_t, BitsetLayout>;

using BitsetStateBuilder = flatmemory::Builder<BitsetStateLayout>;
using BitsetStateConstView = flatmemory::ConstView<BitsetStateLayout>;
using BitsetStateSet = flatmemory::UnorderedSet<BitsetStateLayout>;

/**
 * Implementation class
 */
template<IsPlanningModeTag P>
class Builder<StateDispatcher<BitsetStateTag, P>> :
    public IBuilder<Builder<StateDispatcher<BitsetStateTag, P>>>,
    public IStateBuilder<Builder<StateDispatcher<BitsetStateTag, P>>>,
    public IBitsetStateBuilder<Builder<StateDispatcher<BitsetStateTag, P>>>
{
private:
    BitsetStateBuilder m_builder;

    /* Implement IBuilder interface */
    template<typename>
    friend class IBuilder;

    [[nodiscard]] BitsetStateBuilder& get_flatmemory_builder_impl() { return m_builder; }
    [[nodiscard]] const BitsetStateBuilder& get_flatmemory_builder_impl() const { return m_builder; }

    /* Implement IStateBuilder interface */
    template<typename>
    friend class IStateBuilder;

    [[nodiscard]] uint32_t& get_id_impl() { return m_builder.get<0>(); }

    /* Implement IBitsetStateBuilder interface */
    template<typename>
    friend class IBitsetStateBuilder;

    [[nodiscard]] Bitset& get_atoms_bitset_impl() { return m_builder.get<1>(); }
};

/**
 * Implementation class
 *
 * Reads the memory layout generated by the lifted state builder.
 */
template<IsPlanningModeTag P>
class ConstView<StateDispatcher<BitsetStateTag, P>> :
    public IConstView<ConstView<StateDispatcher<BitsetStateTag, P>>>,
    public IStateView<ConstView<StateDispatcher<BitsetStateTag, P>>>,
    public IBitsetStateView<ConstView<StateDispatcher<BitsetStateTag, P>>>
{
private:
    BitsetStateConstView m_view;

    /* Implement IView interface */
    template<typename>
    friend class IConstView;

    [[nodiscard]] bool are_equal_impl(const ConstView& other) const { return get_atoms_bitset_impl() == other.get_atoms_bitset_impl(); }

    [[nodiscard]] size_t hash_impl() const { return get_atoms_bitset_impl().hash(); }

    /* Implement IStateView interface */
    template<typename>
    friend class IStateView;

    [[nodiscard]] uint32_t get_id_impl() const { return m_view.get<0>(); }

    /* Implement IBitsetStateView interface*/
    template<typename>
    friend class IBitsetStateView;

    [[nodiscard]] ConstBitsetView get_atoms_bitset_impl() const { return m_view.get<1>(); }

public:
    explicit ConstView(BitsetStateConstView view) : m_view(view) {}

    bool contains(const GroundAtom& ground_atom) { return get_atoms_bitset_impl().get(ground_atom->get_identifier()); }

    bool literal_holds(const GroundLiteral& literal) { return literal->is_negated() != contains(literal->get_atom()); }

    bool literals_hold(const GroundLiteralList& literals)
    {
        for (const auto& literal : literals)
        {
            if (!literal_holds(literal))
            {
                return false;
            }
        }

        return true;
    }
};
}

namespace std
{
// Inject hash and equality into std namespace
template<>
struct hash<mimir::BitsetStateConstView>
{
    std::size_t operator()(const mimir::BitsetStateConstView& view) const
    {
        const auto bitset_view = view.get<1>();
        return bitset_view.hash();
    }
};

template<>
struct equal_to<mimir::BitsetStateConstView>
{
    bool operator()(const mimir::BitsetStateConstView& view_left, const mimir::BitsetStateConstView& view_right) const
    {
        const auto bitset_view_left = view_left.get<1>();
        const auto bitset_view_right = view_left.get<1>();
        return bitset_view_left == bitset_view_right;
    }
};
}

#endif
