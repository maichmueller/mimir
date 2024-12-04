#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "trampolines.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void register_classes(py::module& m)
{
    class_<RequirementsImpl>(m, "Requirements");
    class_<ObjectImpl>(m, "Object");
    class_<VariableImpl>(m, "Variable");
    class_<TermImpl>(m, "Term");
    class_<PredicateImpl<Static>>(m, "StaticPredicate");
    class_<PredicateImpl<Fluent>>(m, "FluentPredicate");
    class_<PredicateImpl<Derived>>(m, "DerivedPredicate");
    class_<AtomImpl<Static>>(m, "StaticAtom");
    class_<AtomImpl<Fluent>>(m, "FluentAtom");
    class_<AtomImpl<Derived>>(m, "DerivedAtom");
    class_<PDDLRepositories, std::shared_ptr<PDDLRepositories>>(m, "PDDLRepositories");
    class_<GroundAtomImpl<Static>>(m, "StaticGroundAtom");
    class_<GroundAtomImpl<Fluent>>(m, "FluentGroundAtom");
    class_<GroundAtomImpl<Derived>>(m, "DerivedGroundAtom");
    class_<GroundLiteralImpl<Static>>(m, "StaticGroundLiteral");
    class_<GroundLiteralImpl<Fluent>>(m, "FluentGroundLiteral");
    class_<GroundLiteralImpl<Derived>>(m, "DerivedGroundLiteral");
    class_<LiteralImpl<Static>>(m, "StaticLiteral");
    class_<LiteralImpl<Fluent>>(m, "FluentLiteral");
    class_<AxiomImpl>(m, "Axiom");
    class_<GroundAxiomImpl>(m, "GroundAxiomImpl");
    class_<NumericFluentImpl>(m, "NumericFluent");
    class_<EffectStripsImpl>(m, "EffectStrips");
    class_<EffectConditionalImpl>(m, "EffectConditional");
    class_<GroundEffectStrips>(m, "GroundEffectStrips");
    class_<FlatBitset>(m, "FlatBitset");
    class_<FunctionSkeletonImpl>(m, "FunctionSkeleton");
    class_<FunctionImpl>(m, "Function");
    class_<GroundFunctionImpl>(m, "GroundFunction");
    class_<FunctionExpressionNumberImpl>(m, "FunctionExpressionNumber");
    class_<FunctionExpressionBinaryOperatorImpl>(m, "FunctionExpressionBinaryOperator");
    class_<FunctionExpressionMultiOperatorImpl>(m, "FunctionExpressionMultiOperator");
    class_<FunctionExpressionMinusImpl>(m, "FunctionExpressionMinus");
    class_<FunctionExpressionFunctionImpl>(m, "FunctionExpressionFunction");
    class_<FunctionExpressionImpl>(m, "FunctionExpression");
    class_<GroundFunctionExpressionImpl>(m, "GroundFunctionExpression");
    class_<GroundFunctionExpressionNumberImpl>(m, "GroundFunctionExpressionNumber");
    class_<GroundFunctionExpressionBinaryOperatorImpl>(m, "GroundFunctionExpressionBinaryOperator");
    class_<GroundFunctionExpressionMultiOperatorImpl>(m, "GroundFunctionExpressionMultiOperator");
    class_<GroundFunctionExpressionMinusImpl>(m, "GroundFunctionExpressionMinus");
    class_<GroundFunctionExpressionFunctionImpl>(m, "GroundFunctionExpressionFunction");
    class_<OptimizationMetricImpl>(m, "OptimizationMetric");
    class_<Plan>(m, "Plan");
    class_<ActionImpl>(m, "Action");
    class_<GroundActionImpl>(m, "GroundAction");
    class_<DomainImpl>(m, "Domain");
    class_<ProblemImpl>(m, "Problem");
    class_<PDDLParser>(m, "PDDLParser");
    class_<StateImpl>(m, "State");
    class_<StateRepository, std::shared_ptr<StateRepository>>(m, "StateRepository");
    class_<GroundConditionStrips>(m, "GroundConditionStrips");
    class_<GroundEffectConditional>(m, "GroundEffectConditional");
    class_<LiftedConjunctionGrounder, std::shared_ptr<LiftedConjunctionGrounder>>(m, "LiftedConjunctionGrounder");
    class_<IApplicableActionGenerator, std::shared_ptr<IApplicableActionGenerator>>(m, "IApplicableActionGenerator");
    class_<ILiftedApplicableActionGeneratorEventHandler, std::shared_ptr<ILiftedApplicableActionGeneratorEventHandler>>(
        m,
        "ILiftedApplicableActionGeneratorEventHandler");
    class_<DefaultLiftedApplicableActionGeneratorEventHandler,
           ILiftedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DefaultLiftedApplicableActionGeneratorEventHandler>>(m, "DefaultLiftedApplicableActionGeneratorEventHandler");
    class_<DebugLiftedApplicableActionGeneratorEventHandler,
           ILiftedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DebugLiftedApplicableActionGeneratorEventHandler>>(m, "DebugLiftedApplicableActionGeneratorEventHandler");
    class_<LiftedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<LiftedApplicableActionGenerator>>(m, "LiftedApplicableActionGenerator");
    class_<IGroundedApplicableActionGeneratorEventHandler, std::shared_ptr<IGroundedApplicableActionGeneratorEventHandler>>(
        m,
        "IGroundedApplicableActionGeneratorEventHandler");
    class_<DefaultGroundedApplicableActionGeneratorEventHandler,
           IGroundedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DefaultGroundedApplicableActionGeneratorEventHandler>>(m, "DefaultGroundedApplicableActionGeneratorEventHandler");
    class_<DebugGroundedApplicableActionGeneratorEventHandler,
           IGroundedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DebugGroundedApplicableActionGeneratorEventHandler>>(m, "DebugGroundedApplicableActionGeneratorEventHandler");
    class_<GroundedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<GroundedApplicableActionGenerator>>(
        m,
        "GroundedApplicableActionGenerator");
    class_<IHeuristic, IPyHeuristic, std::shared_ptr<IHeuristic>>(m, "IHeuristic");
    class_<BlindHeuristic, IHeuristic, std::shared_ptr<BlindHeuristic>>(m, "BlindHeuristic");
    class_<IAlgorithm, std::shared_ptr<IAlgorithm>>(m, "IAlgorithm");
    class_<AStarAlgorithmStatistics>(m, "AStarAlgorithmStatistics");
    class_<IAStarAlgorithmEventHandler, std::shared_ptr<IAStarAlgorithmEventHandler>>(m, "IAStarAlgorithmEventHandler");
    class_<DefaultAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DefaultAStarAlgorithmEventHandler>>(
        m,
        "DefaultAStarAlgorithmEventHandler");
    class_<DebugAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DebugAStarAlgorithmEventHandler>>(m,
                                                                                                                           "DebugAStarAlgorithmEventHandler");
    class_<DynamicAStarAlgorithmEventHandlerBase,
           IAStarAlgorithmEventHandler,
           IPyDynamicAStarAlgorithmEventHandlerBase,
           std::shared_ptr<DynamicAStarAlgorithmEventHandlerBase>>(m, "AStarAlgorithmEventHandlerBase");
    class_<AStarAlgorithm, IAlgorithm, std::shared_ptr<AStarAlgorithm>>(m, "AStarAlgorithm");
    class_<BrFSAlgorithmStatistics>(m, "BrFSAlgorithmStatistics");
    class_<IBrFSAlgorithmEventHandler, std::shared_ptr<IBrFSAlgorithmEventHandler>>(m, "IBrFSAlgorithmEventHandler");
    class_<DefaultBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DefaultBrFSAlgorithmEventHandler>>(m,
                                                                                                                            "DefaultBrFSAlgorithmEventHandler");
    class_<DebugBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DebugBrFSAlgorithmEventHandler>>(m, "DebugBrFSAlgorithmEventHandler");
    class_<BrFSAlgorithm, IAlgorithm, std::shared_ptr<BrFSAlgorithm>>(m, "BrFSAlgorithm");
    class_<TupleIndexMapper, std::shared_ptr<TupleIndexMapper>>(m, "TupleIndexMapper");
    class_<IWAlgorithmStatistics>(m, "IWAlgorithmStatistics");
    class_<IIWAlgorithmEventHandler, std::shared_ptr<IIWAlgorithmEventHandler>>(m, "IIWAlgorithmEventHandler");
    class_<DefaultIWAlgorithmEventHandler, IIWAlgorithmEventHandler, std::shared_ptr<DefaultIWAlgorithmEventHandler>>(m, "DefaultIWAlgorithmEventHandler");
    class_<IWAlgorithm, IAlgorithm, std::shared_ptr<IWAlgorithm>>(m, "IWAlgorithm");
    class_<SIWAlgorithmStatistics>(m, "SIWAlgorithmStatistics");
    class_<ISIWAlgorithmEventHandler, std::shared_ptr<ISIWAlgorithmEventHandler>>(m, "ISIWAlgorithmEventHandler");
    class_<DefaultSIWAlgorithmEventHandler, ISIWAlgorithmEventHandler, std::shared_ptr<DefaultSIWAlgorithmEventHandler>>(m, "DefaultSIWAlgorithmEventHandler");
    class_<SIWAlgorithm, IAlgorithm, std::shared_ptr<SIWAlgorithm>>(m, "SIWAlgorithm");
    class_<StateVertex>(m, "StateVertex");
    class_<GroundActionEdge>(m, "GroundActionEdge");
    class_<GroundActionsEdge>(m, "GroundActionsEdge");
    class_<StateSpaceOptions>(m, "StateSpaceOptions");
    class_<StateSpacesOptions>(m, "StateSpacesOptions");
    class_<StateSpace, std::shared_ptr<StateSpace>>(m, "StateSpace");
    class_<FaithfulAbstractionOptions>(m, "FaithfulAbstractionOptions");
    class_<FaithfulAbstractionsOptions>(m, "FaithfulAbstractionsOptions");
    class_<FaithfulAbstractStateVertex>(m, "FaithfulAbstractStateVertex");
    class_<FaithfulAbstraction, std::shared_ptr<FaithfulAbstraction>>(m, "FaithfulAbstraction");
    class_<GlobalFaithfulAbstractState>(m, "GlobalFaithfulAbstractState");
    class_<GlobalFaithfulAbstraction, std::shared_ptr<GlobalFaithfulAbstraction>>(m, "GlobalFaithfulAbstraction");
    class_<Abstraction, std::shared_ptr<Abstraction>>(m, "Abstraction");
    class_<EmptyVertex>(m, "EmptyVertex");
    class_<EmptyEdge>(m, "EmptyEdge");
    class_<StaticDigraph>(m, "StaticDigraph");
    class_<nauty_wrapper::Certificate, std::shared_ptr<nauty_wrapper::Certificate>>(m, "NautyCertificate");
    class_<nauty_wrapper::DenseGraph>(m, "NautyDenseGraph");
    class_<nauty_wrapper::SparseGraph>(m, "NautySparseGraph");
    class_<ColorFunction>(m, "ColorFunction");
    class_<ProblemColorFunction, ColorFunction>(m, "ProblemColorFunction");
    class_<ColoredVertex>(m, "ColoredVertex");
    class_<StaticVertexColoredDigraph>(m, "StaticVertexColoredDigraph");
    class_<TupleGraphVertex>(m, "TupleGraphVertex");
    class_<TupleGraph>(m, "TupleGraph");
    class_<TupleGraphFactory>(m, "TupleGraphFactory");
    class_<ObjectGraphPruningStrategy>(m, "ObjectGraphPruningStrategy");
    class_<color_refinement::Certificate>(m, "CertificateColorRefinement");
    class_<kfwl::Certificate<2>>(m, "Certificate2FWL");
    class_<kfwl::Certificate<3>>(m, "Certificate3FWL");
    class_<kfwl::Certificate<4>>(m, "Certificate4FWL");
    class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction");

    py::bind_vector<GroundActionList>(m, "GroundActionList");
    py::bind_vector<EffectConditionalList>(m, "EffectConditionalList");
    py::bind_vector<GroundEffectConditionalList>(m, "GroundEffectConditionalList");
    py::bind_vector<GroundFunctionExpressionList>(m, "GroundFunctionExpressionList");
    py::bind_vector<StateList>(m, "StateList");
    py::bind_vector<ActionList>(m, "ActionList");
    py::bind_vector<ProblemList>(m, "ProblemList");
    py::bind_vector<AxiomList>(m, "AxiomList");
    py::bind_vector<VariableList>(m, "VariableList");
    py::bind_vector<DomainList>(m, "DomainList");
    py::bind_vector<FlatIndexList>(m, "FlatIndexList");
    py::bind_vector<FunctionExpressionList>(m, "FunctionExpressionList");
    py::bind_vector<FunctionSkeletonList>(m, "FunctionSkeletonList");
    py::bind_vector<FunctionList>(m, "FunctionList");
    py::bind_vector<GroundFunctionList>(m, "GroundFunctionList");
    py::bind_vector<NumericFluentList>(m, "NumericFluentList");
    py::bind_vector<ObjectList>(m, "ObjectList");
    py::bind_vector<TermList>(m, "TermList");
    for_each_tag(
        [&]<typename Tag>(Tag, std::string tag = tag_name<Tag>())
        {
            class_<GroundEffectLiteral<Tag>>(m, "GroundEffect" + tag + "Literal");
            py::bind_vector<GroundEffectLiteralList<Tag>>(m, "GroundEffect" + tag + "LiteralList");
            py::bind_vector<AtomList<Tag>>(m, tag + "AtomList");
            py::bind_vector<LiteralList<Tag>>(m, tag + "LiteralList");
            py::bind_vector<GroundLiteralList<Tag>>(m, tag + "GroundLiteralList");
            py::bind_vector<GroundAtomList<Tag>>(m, tag + "GroundAtomList");
            py::bind_vector<PredicateList<Tag>>(m, tag + "PredicateList");
            py::bind_map<ToPredicateMap<std::string, Tag>>(m, "StringTo" + tag + "PredicateMap");
        });
}