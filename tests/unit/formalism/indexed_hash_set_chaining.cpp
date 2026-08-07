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

/// Gate for T1 of docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md: the local loki patch
/// (dependencies/loki/patches/0001-indexed-hash-set-multi-level-chaining.patch) that lets
/// `loki::IndexedHashSet` chain more than one level deep. Lifted grounding overlays need the
/// three-level chain domain -> problem -> overlay; stock loki throws on construction and, even
/// with the throw removed, resolves indices by reaching into `m_parent->m_vector` directly, which
/// is wrong at depth two.
///
/// The tests below use `ObjectRepository` because it is the simplest real repository in the hana
/// map: patching the primitive is only useful if it holds for the types Mimir actually interns.

#include "mimir/formalism/repositories.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace mimir::tests
{

using namespace mimir::formalism;

#ifndef LOKI_INDEXED_HASH_SET_SUPPORTS_MULTI_LEVEL_CHAINING
#error "The loki multi-level chaining patch is not present in the loki headers being compiled \
against. Re-run CMake configure (cmake/configure_loki.cmake applies it), or wipe \
dependencies/installs and rebuild the dependencies."
#endif

namespace
{
/// Interns `name` with no type bases; returns the interned pointer.
Object intern(ObjectRepository& repository, const std::string& name) { return repository.get_or_create(name, TypeList {}); }
}

TEST(MimirTests, MimirFormalismIndexedHashSetThreeLevelChainTest)
{
    /* A three-level chain must be constructible at all. Stock loki throws here. */
    auto domain_level = ObjectRepository();
    auto problem_level = ObjectRepository(&domain_level);
    auto overlay_level = ObjectRepository(&problem_level);

    const auto d0 = intern(domain_level, "d0");
    const auto d1 = intern(domain_level, "d1");
    const auto p0 = intern(problem_level, "p0");
    const auto o0 = intern(overlay_level, "o0");
    const auto o1 = intern(overlay_level, "o1");

    /* Each level continues its ancestors' index space rather than restarting at 0. */
    EXPECT_EQ(d0->get_index(), 0);
    EXPECT_EQ(d1->get_index(), 1);
    EXPECT_EQ(p0->get_index(), 2);
    EXPECT_EQ(o0->get_index(), 3);
    EXPECT_EQ(o1->get_index(), 4);

    /* `size()` counts the whole chain; writes never propagate upward. */
    EXPECT_EQ(domain_level.size(), 2);
    EXPECT_EQ(problem_level.size(), 3);
    EXPECT_EQ(overlay_level.size(), 5);
    EXPECT_EQ(overlay_level.parent_size(), 3);

    /* `operator[]` and `at` resolve at all three levels, not just the innermost two: index 0 and
       1 live two levels up, which is exactly what unpatched loki gets wrong. */
    EXPECT_EQ(overlay_level[0], d0);
    EXPECT_EQ(overlay_level[1], d1);
    EXPECT_EQ(overlay_level[2], p0);
    EXPECT_EQ(overlay_level[3], o0);
    EXPECT_EQ(overlay_level[4], o1);
    EXPECT_EQ(overlay_level.at(0), d0);
    EXPECT_EQ(overlay_level.at(2), p0);
    EXPECT_EQ(overlay_level.at(4), o1);
    EXPECT_THROW((void) overlay_level.at(5), std::out_of_range);

    /* The intermediate level still sees only its own prefix of the chain. */
    EXPECT_EQ(problem_level[0], d0);
    EXPECT_EQ(problem_level[2], p0);
    EXPECT_THROW((void) problem_level.at(3), std::out_of_range);

    /* Iteration walks the chain outermost-ancestor-first, in index order. */
    auto iterated = std::vector<Object> {};
    for (const auto& object : overlay_level)
    {
        iterated.push_back(&object);
    }
    EXPECT_EQ(iterated, (std::vector<Object> { d0, d1, p0, o0, o1 }));
}

TEST(MimirTests, MimirFormalismIndexedHashSetThreeLevelChainDedupTest)
{
    auto domain_level = ObjectRepository();
    auto problem_level = ObjectRepository(&domain_level);
    auto overlay_level = ObjectRepository(&problem_level);

    const auto d0 = intern(domain_level, "d0");
    const auto p0 = intern(problem_level, "p0");

    /* A `get_or_create` on the innermost level must find a grandparent's entry and return *that*
       pointer, not intern a shadowing copy. Identity is pointer identity everywhere in Mimir. */
    EXPECT_EQ(intern(overlay_level, "d0"), d0);
    EXPECT_EQ(intern(overlay_level, "p0"), p0);
    EXPECT_EQ(overlay_level.size(), 2);
    EXPECT_TRUE(overlay_level.get_vector().empty());

    /* Same for `find`, at every depth. */
    EXPECT_EQ(overlay_level.find(*d0), d0);
    EXPECT_EQ(overlay_level.find(*p0), p0);
    EXPECT_EQ(problem_level.find(*d0), d0);

    /* A genuinely new element lands locally and leaves the ancestors untouched. */
    const auto o0 = intern(overlay_level, "o0");
    EXPECT_EQ(o0->get_index(), 2);
    EXPECT_EQ(domain_level.size(), 1);
    EXPECT_EQ(problem_level.size(), 2);
    EXPECT_EQ(overlay_level.get_vector().size(), 1);
    EXPECT_EQ(problem_level.find(*o0), nullptr);
    EXPECT_EQ(domain_level.find(*o0), nullptr);
}

TEST(MimirTests, MimirFormalismIndexedHashSetSiblingOverlaysTest)
{
    auto domain_level = ObjectRepository();
    auto problem_level = ObjectRepository(&domain_level);
    auto overlay_a = ObjectRepository(&problem_level);
    auto overlay_b = ObjectRepository(&problem_level);

    intern(domain_level, "d0");
    intern(problem_level, "p0");

    /* Sibling overlays hand the *same* index to *different* objects. This is the invariant that
       forbids comparing worker-local indices across overlays (plan section 1.2 / pitfall 4). */
    const auto a0 = intern(overlay_a, "a0");
    const auto b0 = intern(overlay_b, "b0");
    EXPECT_EQ(a0->get_index(), b0->get_index());
    EXPECT_NE(a0, b0);
    EXPECT_EQ(overlay_a[a0->get_index()], a0);
    EXPECT_EQ(overlay_b[b0->get_index()], b0);

    /* Overlays grow independently and neither is visible from the other or from the parent. */
    intern(overlay_a, "a1");
    EXPECT_EQ(overlay_a.size(), 4);
    EXPECT_EQ(overlay_b.size(), 3);
    EXPECT_EQ(problem_level.size(), 2);
    EXPECT_EQ(overlay_b.find(*a0), nullptr);
    EXPECT_EQ(overlay_a.find(*b0), nullptr);

    /* Inherited entries stay shared and keep their inherited indices in both overlays. */
    EXPECT_EQ(overlay_a[0], overlay_b[0]);
    EXPECT_EQ(overlay_a[1], overlay_b[1]);
}
}
