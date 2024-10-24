# Import all classes for better IDE support

# Common
from _pymimir import (
    FlatBitset,
)

# Algorithms
from _pymimir import (
    NautyCertificate,
    NautyDenseGraph,
    NautySparseGraph,
)

# Formalism
from _pymimir import (
    Action,
    ActionList,
    AssignOperatorEnum,
    Axiom,
    AxiomList,
    BinaryOperatorEnum,
    DerivedAtom,
    DerivedAtomList,
    DerivedGroundAtom,
    DerivedGroundAtomList,
    DerivedGroundLiteral,
    DerivedGroundLiteralList,
    DerivedLiteral,
    DerivedLiteralList,
    DerivedPredicate,
    DerivedPredicateList,
    Domain,
    DomainList,
    EffectSimple,
    EffectSimpleList,
    EffectComplex,
    EffectComplexList,
    FluentAtom,
    FluentAtomList,
    FluentGroundAtom,
    FluentGroundAtomList,
    FluentGroundLiteral,
    FluentGroundLiteralList,
    FluentLiteral,
    FluentLiteralList,
    FluentPredicate,
    FluentPredicateList,
    Function,
    FunctionList,
    FunctionExpression,
    FunctionExpressionVariantList,
    FunctionExpressionBinaryOperator,
    FunctionExpressionFunction,
    FunctionExpressionMinus,
    FunctionExpressionMultiOperator,
    FunctionExpressionNumber,
    FunctionSkeleton,
    FunctionSkeletonList,
    GroundFunction,
    GroundFunctionList,
    GroundFunctionExpression,
    GroundFunctionExpressionVariantList,
    GroundFunctionExpressionBinaryOperator,
    GroundFunctionExpressionFunction,
    GroundFunctionExpressionMinus,
    GroundFunctionExpressionMultiOperator,
    GroundFunctionExpressionNumber,
    MultiOperatorEnum,
    NumericFluent,
    NumericFluentList,
    Object,
    ObjectList,
    OptimizationMetric,
    OptimizationMetricEnum,
    PDDLFactories,
    PDDLParser,
    Problem,
    ProblemList,
    Requirements,
    RequirementEnum,
    StaticAtom,
    StaticAtomList,
    StaticGroundAtom,
    StaticGroundAtomList,
    StaticGroundLiteral,
    StaticGroundLiteralList,
    StaticLiteral,
    StaticLiteralList,
    StaticPredicate,
    StaticPredicateList,
    Term,
    TermVariantList,
    TermObject,
    TermVariable,
    Variable,
    VariableList
)

# Search
from _pymimir import (
    AStarAlgorithm,
    AStarAlgorithmEventHandlerBase,
    AStarAlgorithmStatistics,
    BlindHeuristic,
    BrFSAlgorithm,
    BrFSAlgorithmStatistics,
    ConditionalEffect,
    DebugAStarAlgorithmEventHandler,
    DebugBrFSAlgorithmEventHandler,
    DebugGroundedApplicableActionGeneratorEventHandler,
    DebugLiftedApplicableActionGeneratorEventHandler,
    DefaultBrFSAlgorithmEventHandler,
    DefaultAStarAlgorithmEventHandler,
    DefaultGroundedApplicableActionGeneratorEventHandler,
    DefaultIWAlgorithmEventHandler,
    DefaultLiftedApplicableActionGeneratorEventHandler,
    DefaultSIWAlgorithmEventHandler,
    FlatSimpleEffect,
    GroundAction,
    GroundActionList,
    GroundActionSpan,
    GroundedApplicableActionGenerator,
    IApplicableActionGenerator,
    IAlgorithm,
    IAStarAlgorithmEventHandler,
    IBrFSAlgorithmEventHandler,
    IIWAlgorithmEventHandler,
    IGroundedApplicableActionGeneratorEventHandler,
    IHeuristic,
    ILiftedApplicableActionGeneratorEventHandler,
    ISIWAlgorithmEventHandler,
    IWAlgorithm,
    IWAlgorithmStatistics,
    LiftedApplicableActionGenerator,
    LiftedConjunctionGrounder,
    SearchNodeStatus,
    SearchStatus,
    SIWAlgorithm,
    SIWAlgorithmStatistics,
    State,
    StateList,
    StateRepository,
    StateSpan,
    IndexGroupedVector,
    StripsActionEffect,
    StripsActionPrecondition,
    TupleIndexMapper
)

# Dataset
from _pymimir import (
    Abstraction,
    StateVertex,
    GroundActionEdge,
    GroundActionsEdge,
    FaithfulAbstraction,
    FaithfulAbstractionOptions,
    FaithfulAbstractionsOptions,
    FaithfulAbstractStateVertex,
    GlobalFaithfulAbstractState,
    GlobalFaithfulAbstraction,
    StateSpace,
    StateSpaceOptions,
    StateSpacesOptions
)

# Graphs (classes)
from _pymimir import (
    ColoredVertex,
    ColorFunction,
    EmptyVertex,
    EmptyEdge,
    ObjectGraphPruningStrategy,
    ObjectGraphPruningStrategyEnum,
    ProblemColorFunction,
    StaticDigraph,
    StaticVertexColoredDigraph,
    TupleGraphVertex,
    TupleGraphVertexSpan,
    TupleGraphVertexIndexGroupedVector,
    TupleGraph,
    TupleGraphFactory
)

# Graphs (free functions)
from _pymimir import (
    create_object_graph
)

from .hints import *


def parse_pddl(domain_path, problem_path):
    parser = PDDLParser(domain_path, problem_path)
    return parser.get_domaiin(), parser.get_problem(), parser.get_pddl_factories()