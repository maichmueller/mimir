#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_DENSE_LIFTED_ASSIGNMENT_SET_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_DENSE_LIFTED_ASSIGNMENT_SET_HPP_

#include "mimir/formalism/declarations.hpp"

#include <cstdint>
#include <vector>

namespace mimir
{
namespace consistency_graph
{
class Vertex;
class Edge;
}

// TODO: this will make the function m_f easier to understand.
// It will also make evaluations more efficient since they are closer together in memory.
class UnaryAssignmentSet
{
private:
    Problem m_problem;
    std::vector<std::vector<bool>> m_f;

    struct UnaryAssignment
    {
        uint32_t position;
        uint32_t object;
    };

    static size_t get_assignment_position(const UnaryAssignment& assignment, uint32_t arity, uint32_t num_objects);

    static size_t get_num_assignments(uint32_t arity, uint32_t num_objects);

public:
    /// @brief Construct from a given set of ground atoms.
    UnaryAssignmentSet(Problem problem, const GroundAtomList& atoms);

    /// @brief Return true iff all literals are consistent with
    /// 1. the assignment set, and 2. the vertex of the consistency graph.
    ///
    /// The meaning is that the assignment [x/o] is a legal assignment,
    /// and therefore the vertex must be part of the consistency graph.
    bool literal_all_consistent(const std::vector<Literal>& literals, const consistency_graph::Vertex& consistent_vertex) const;
};

class BinaryAssignmentSet
{
private:
    Problem m_problem;
    std::vector<std::vector<bool>> m_f;

    struct BinaryAssignment
    {
        uint32_t first_position;
        uint32_t first_object;
        uint32_t second_position;
        uint32_t second_object;
    };

    static size_t get_assignment_position(const BinaryAssignment& assignment, uint32_t arity, uint32_t num_objects);

    static size_t get_num_assignments(uint32_t arity, uint32_t num_objects);

public:
    /// @brief Construct from a given set of ground atoms.
    BinaryAssignmentSet(Problem problem, const GroundAtomList& atoms);

    /// @brief Return true iff all literals are consistent with
    /// 1. the assignment set, and 2. the edge of the consistency graph.
    ///
    /// The meaning of the result being true is that the edge remains consistent.
    bool literal_all_consistent(const std::vector<Literal>& literals, const consistency_graph::Edge& consistent_edge) const;
};

/// @brief AssignmentSet is a helper class representing a set of functions
/// f : Predicates x Params(A) x Object x Params(A) x Object -> {true, false} where
///   1. f(p,i,o,j,o') = true iff there exists an atom p(...,o_i,...,o'_j)
///   2. f(p,i,o,-1,-1) = true iff there exists an atom p(...,o_i,...)
/// with respective meanings
///   1. the assignment [i/o], [j/o'] is consistent
///   2. the assignment [i/o] is consistent
///
/// We say that an assignment set is static if all atoms it considers are static.
class AssignmentSet
{
private:
    Problem m_problem;

    // The underlying function
    std::vector<std::vector<bool>> m_f;

public:
    /// @brief Construct from a given set of ground atoms.
    AssignmentSet(Problem problem, const GroundAtomList& atoms);

    /// @brief Return true iff all literals are consistent with
    /// 1. the assignment set, and 2. the edge of the consistency graph.
    ///
    /// The meaning of the result being true is that the edge remains consistent.
    bool literal_all_consistent(const std::vector<Literal>& literals, const consistency_graph::Edge& consistent_edge) const;

    /// @brief Return true iff all literals are consistent with
    /// 1. the assignment set, and 2. the vertex of the consistency graph.
    ///
    /// The meaning is that the assignment [x/o] is a legal assignment,
    /// and therefore the vertex must be part of the consistency graph.
    /// @param literals
    /// @param consistent_vertex
    /// @return
    bool literal_all_consistent(const std::vector<Literal>& literals, const consistency_graph::Vertex& consistent_vertex) const;
};

}

#endif
