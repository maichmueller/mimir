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

#ifndef MIMIR_LANGUAGES_DESCRIPTION_LOGICS_CONSTRUCTORS_HPP_
#define MIMIR_LANGUAGES_DESCRIPTION_LOGICS_CONSTRUCTORS_HPP_

#include "mimir/languages/description_logics/constructors_interface.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <vector>

namespace mimir::dl
{

/**
 * Forward declarations
 */

template<typename T>
class ConstructorRepository;

/**
 * Concepts
 */

template<PredicateCategory P>
class ConceptPredicateState : public Concept
{
private:
    size_t m_id;
    Predicate<P> m_predicate;

    ConceptPredicateState(size_t id, Predicate<P> predicate);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    ConceptPredicateState(const ConceptPredicateState& other) = delete;
    ConceptPredicateState& operator=(const ConceptPredicateState& other) = delete;
    ConceptPredicateState(ConceptPredicateState&& other) = default;
    ConceptPredicateState& operator=(ConceptPredicateState&& other) = default;

    bool operator==(const ConceptPredicateState& other) const;
    bool is_equal(const Concept& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const ConceptVisitor& visitor) const override;

    size_t get_id() const override;

    Predicate<P> get_predicate() const;
};

template<PredicateCategory P>
class ConceptPredicateGoal : public Concept
{
private:
    size_t m_id;
    Predicate<P> m_predicate;

    ConceptPredicateGoal(size_t id, Predicate<P> predicate);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    ConceptPredicateGoal(const ConceptPredicateGoal& other) = delete;
    ConceptPredicateGoal& operator=(const ConceptPredicateGoal& other) = delete;
    ConceptPredicateGoal(ConceptPredicateGoal&& other) = default;
    ConceptPredicateGoal& operator=(ConceptPredicateGoal&& other) = default;

    bool operator==(const ConceptPredicateGoal& other) const;
    bool is_equal(const Concept& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const ConceptVisitor& visitor) const override;

    size_t get_id() const override;

    Predicate<P> get_predicate() const;
};

class ConceptAnd : public Concept
{
private:
    size_t m_id;
    const Concept& m_concept_left;
    const Concept& m_concept_right;

    ConceptAnd(size_t id, const Concept& concept_left, const Concept& concept_right);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    ConceptAnd(const ConceptAnd& other) = delete;
    ConceptAnd& operator=(const ConceptAnd& other) = delete;
    ConceptAnd(ConceptAnd&& other) = default;
    ConceptAnd& operator=(ConceptAnd&& other) = default;

    bool operator==(const ConceptAnd& other) const;
    bool is_equal(const Concept& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const ConceptVisitor& visitor) const override;

    size_t get_id() const override;

    const Concept& get_concept_left() const;
    const Concept& get_concept_right() const;
};

/**
 * Roles
 */

template<PredicateCategory P>
class RolePredicateState : public Role
{
private:
    size_t m_id;
    Predicate<P> m_predicate;

    RolePredicateState(size_t id, Predicate<P> predicate);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    RolePredicateState(const RolePredicateState& other) = delete;
    RolePredicateState& operator=(const RolePredicateState& other) = delete;
    RolePredicateState(RolePredicateState&& other) = default;
    RolePredicateState& operator=(RolePredicateState&& other) = default;

    bool operator==(const RolePredicateState& other) const;
    bool is_equal(const Role& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const RoleVisitor& visitor) const override;

    size_t get_id() const override;

    Predicate<P> get_predicate() const;
};

template<PredicateCategory P>
class RolePredicateGoal : public Role
{
private:
    size_t m_id;
    Predicate<P> m_predicate;

    RolePredicateGoal(size_t id, Predicate<P> predicate);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    RolePredicateGoal(const RolePredicateGoal& other) = delete;
    RolePredicateGoal& operator=(const RolePredicateGoal& other) = delete;
    RolePredicateGoal(RolePredicateGoal&& other) = default;
    RolePredicateGoal& operator=(RolePredicateGoal&& other) = default;

    bool operator==(const RolePredicateGoal& other) const;
    bool is_equal(const Role& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const RoleVisitor& visitor) const override;

    size_t get_id() const override;

    Predicate<P> get_predicate() const;
};

class RoleAnd : public Role
{
private:
    size_t m_id;
    const Role& m_role_left;
    const Role& m_role_right;

    RoleAnd(size_t id, const Role& role_left, const Role& role_right);

    template<typename T>
    friend class ConstructorRepository;

public:
    // Users are not supposed to move these directly.
    RoleAnd(const RoleAnd& other) = delete;
    RoleAnd& operator=(const RoleAnd& other) = delete;
    RoleAnd(RoleAnd&& other) = default;
    RoleAnd& operator=(RoleAnd&& other) = default;

    bool operator==(const RoleAnd& other) const;
    bool is_equal(const Role& other) const override;
    size_t hash() const override;

    void evaluate(EvaluationContext& context) const override;

    bool accept(const RoleVisitor& visitor) const override;

    size_t get_id() const override;

    const Role& get_role_left() const;
    const Role& get_role_right() const;
};

}

#endif
