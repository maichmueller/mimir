#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "trampolines.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace mimir;


#define ENTRY(key, ...) TypeRegister::define(key,  __VA_ARGS__(m, key))


void init_pymimir(py::module& m)
{

    ENTRY("RequirementEnum", py::enum_<loki::RequirementEnum>);
    ENTRY("AssignOperatorEnum", py::enum_<loki::AssignOperatorEnum>);
    ENTRY("BinaryOperatorEnum", py::enum_<loki::BinaryOperatorEnum>);
    ENTRY("MultiOperatorEnum", py::enum_<loki::MultiOperatorEnum>);
    ENTRY("OptimizationMetricEnum", py::enum_<loki::OptimizationMetricEnum>);
    ENTRY("SearchNodeStatus", py::enum_<SearchNodeStatus>);
    ENTRY("SearchStatus", py::enum_<SearchStatus>);
    ENTRY("ObjectGraphPruningStrategyEnum", py::enum_<ObjectGraphPruningStrategyEnum>);


    ENTRY("Requirements", py::class_<RequirementsImpl>);
    ENTRY("Object", py::class_<ObjectImpl>);
    ENTRY("Variable", py::class_<VariableImpl>);
    ENTRY("TermObject", py::class_<TermObjectImpl>);
    ENTRY("TermVariable", py::class_<TermVariableImpl>);
    ENTRY("Term", py::class_<pymimir::TermVariant>);
    ENTRY("StaticPredicate", py::class_<PredicateImpl<Static>>);
    ENTRY("FluentPredicate", py::class_<PredicateImpl<Fluent>>);
    ENTRY("DerivedPredicate", py::class_<PredicateImpl<Derived>>);
    ENTRY("StaticAtom", py::class_<AtomImpl<Static>>);
    ENTRY("FluentAtom", py::class_<AtomImpl<Fluent>>);
    ENTRY("DerivedAtom", py::class_<AtomImpl<Derived>>);
    ENTRY("PDDLFactories", py::class_<PDDLFactories, std::shared_ptr<PDDLFactories>>);
    ENTRY("StaticGroundAtom", py::class_<GroundAtomImpl<Static>>);
    ENTRY("FluentGroundAtom", py::class_<GroundAtomImpl<Fluent>>);
    ENTRY("DerivedGroundAtom", py::class_<GroundAtomImpl<Derived>>);
    ENTRY("StaticGroundLiteral", py::class_<GroundLiteralImpl<Static>>);
    ENTRY("FluentGroundLiteral", py::class_<GroundLiteralImpl<Fluent>>);
    ENTRY("DerivedGroundLiteral", py::class_<GroundLiteralImpl<Derived>>);
    ENTRY("StaticLiteral", py::class_<LiteralImpl<Static>>);
    ENTRY("FluentLiteral", py::class_<LiteralImpl<Fluent>>);
    ENTRY("DerivedLiteral", py::class_<LiteralImpl<Derived>>);
    ENTRY("Axiom", py::class_<AxiomImpl>);
    ENTRY("NumericFluent", py::class_<NumericFluentImpl>);
    ENTRY("EffectSimple", py::class_<EffectSimpleImpl>);
    ENTRY("EffectComplex", py::class_<EffectComplexImpl>);
    ENTRY("StripsActionEffect", py::class_<StripsActionEffect>);
    ENTRY("SimpleFluentEffect", py::class_<SimpleFluentEffect>);
    ENTRY("FlatBitset", py::class_<FlatBitset>);
    ENTRY("FunctionSkeleton", py::class_<FunctionSkeletonImpl>);
    ENTRY("Function", py::class_<FunctionImpl>);
    ENTRY("GroundFunction", py::class_<GroundFunctionImpl>);
    ENTRY("FunctionExpressionNumber", py::class_<FunctionExpressionNumberImpl>);
    ENTRY("FunctionExpressionBinaryOperator", py::class_<FunctionExpressionBinaryOperatorImpl>);
    ENTRY("FunctionExpressionMultiOperator", py::class_<FunctionExpressionMultiOperatorImpl>);
    ENTRY("FunctionExpressionMinus", py::class_<FunctionExpressionMinusImpl>);
    ENTRY("FunctionExpressionFunction", py::class_<FunctionExpressionFunctionImpl>);
    ENTRY("FunctionExpression", py::class_<pymimir::FunctionExpressionVariant>);
    ENTRY("GroundFunctionExpression", py::class_<pymimir::GroundFunctionExpressionVariant>);
    ENTRY("GroundFunctionExpressionNumber", py::class_<GroundFunctionExpressionNumberImpl>);
    ENTRY("GroundFunctionExpressionBinaryOperator", py::class_<GroundFunctionExpressionBinaryOperatorImpl>);
    ENTRY("GroundFunctionExpressionMultiOperator", py::class_<GroundFunctionExpressionMultiOperatorImpl>);
    ENTRY("GroundFunctionExpressionMinus", py::class_<GroundFunctionExpressionMinusImpl>);
    ENTRY("GroundFunctionExpressionFunction", py::class_<GroundFunctionExpressionFunctionImpl>);
    ENTRY("OptimizationMetric", py::class_<OptimizationMetricImpl>);
    ENTRY("Action", py::class_<ActionImpl>);
    ENTRY("GroundAction", py::class_<GroundActionImpl>);
    ENTRY("Domain", py::class_<DomainImpl>);
    ENTRY("Problem", py::class_<ProblemImpl>);
    ENTRY("PDDLParser", py::class_<PDDLParser>);
    ENTRY("State", py::class_<StateImpl>);
    ENTRY("StateRepository", py::class_<StateRepository, std::shared_ptr<StateRepository>>);
    ENTRY("StripsActionPrecondition", py::class_<StripsActionPrecondition>);
    ENTRY("ConditionalEffect", py::class_<ConditionalEffect>);
    ENTRY("LiftedConjunctionGrounder", py::class_<LiftedConjunctionGrounder, std::shared_ptr<LiftedConjunctionGrounder>>);
    ENTRY("IApplicableActionGenerator", py::class_<IApplicableActionGenerator, std::shared_ptr<IApplicableActionGenerator>>);
    ENTRY("ILiftedApplicableActionGeneratorEventHandler", py::class_<ILiftedApplicableActionGeneratorEventHandler, std::shared_ptr<ILiftedApplicableActionGeneratorEventHandler>>);
    ENTRY("DefaultLiftedApplicableActionGeneratorEventHandler", py::class_<DefaultLiftedApplicableActionGeneratorEventHandler,ILiftedApplicableActionGeneratorEventHandler,std::shared_ptr<DefaultLiftedApplicableActionGeneratorEventHandler>>);
    ENTRY("DebugLiftedApplicableActionGeneratorEventHandler", py::class_<DebugLiftedApplicableActionGeneratorEventHandler,ILiftedApplicableActionGeneratorEventHandler,std::shared_ptr<DebugLiftedApplicableActionGeneratorEventHandler>>);
    ENTRY("LiftedApplicableActionGenerator", py::class_<LiftedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<LiftedApplicableActionGenerator>>);
    ENTRY("IGroundedApplicableActionGeneratorEventHandler", py::class_<IGroundedApplicableActionGeneratorEventHandler, std::shared_ptr<IGroundedApplicableActionGeneratorEventHandler>>);
    ENTRY("DefaultGroundedApplicableActionGeneratorEventHandler", py::class_<DefaultGroundedApplicableActionGeneratorEventHandler,IGroundedApplicableActionGeneratorEventHandler,std::shared_ptr<DefaultGroundedApplicableActionGeneratorEventHandler>>);
    ENTRY("DebugGroundedApplicableActionGeneratorEventHandler", py::class_<DebugGroundedApplicableActionGeneratorEventHandler,IGroundedApplicableActionGeneratorEventHandler,std::shared_ptr<DebugGroundedApplicableActionGeneratorEventHandler>>);
    ENTRY("GroundedApplicableActionGenerator", py::class_<GroundedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<GroundedApplicableActionGenerator>>);
    ENTRY("IHeuristic", py::class_<IHeuristic, IPyHeuristic, std::shared_ptr<IHeuristic>>);
    ENTRY("BlindHeuristic", py::class_<BlindHeuristic, IHeuristic, std::shared_ptr<BlindHeuristic>>);
    ENTRY("IAlgorithm", py::class_<IAlgorithm, std::shared_ptr<IAlgorithm>>);
    ENTRY("AStarAlgorithmStatistics", py::class_<AStarAlgorithmStatistics>);
    ENTRY("IAStarAlgorithmEventHandler", py::class_<IAStarAlgorithmEventHandler, std::shared_ptr<IAStarAlgorithmEventHandler>>);
    ENTRY("DefaultAStarAlgorithmEventHandler", py::class_<DefaultAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DefaultAStarAlgorithmEventHandler>>);
    ENTRY("DebugAStarAlgorithmEventHandler", py::class_<DebugAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DebugAStarAlgorithmEventHandler>>);
    ENTRY("AStarAlgorithmEventHandlerBase", py::class_<DynamicAStarAlgorithmEventHandlerBase,IAStarAlgorithmEventHandler,IPyDynamicAStarAlgorithmEventHandlerBase,std::shared_ptr<DynamicAStarAlgorithmEventHandlerBase>>);
    ENTRY("AStarAlgorithm", py::class_<AStarAlgorithm, IAlgorithm, std::shared_ptr<AStarAlgorithm>>);
    ENTRY("BrFSAlgorithmStatistics", py::class_<BrFSAlgorithmStatistics>);
    ENTRY("IBrFSAlgorithmEventHandler", py::class_<IBrFSAlgorithmEventHandler, std::shared_ptr<IBrFSAlgorithmEventHandler>>);
    ENTRY("DefaultBrFSAlgorithmEventHandler", py::class_<DefaultBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DefaultBrFSAlgorithmEventHandler>>);
    ENTRY("DebugBrFSAlgorithmEventHandler", py::class_<DebugBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DebugBrFSAlgorithmEventHandler>>);
    ENTRY("BrFSAlgorithm", py::class_<BrFSAlgorithm, IAlgorithm, std::shared_ptr<BrFSAlgorithm>>);
    ENTRY("TupleIndexMapper", py::class_<TupleIndexMapper, std::shared_ptr<TupleIndexMapper>>);
    ENTRY("IWAlgorithmStatistics", py::class_<IWAlgorithmStatistics>);
    ENTRY("IIWAlgorithmEventHandler", py::class_<IIWAlgorithmEventHandler, std::shared_ptr<IIWAlgorithmEventHandler>>);
    ENTRY("DefaultIWAlgorithmEventHandler", py::class_<DefaultIWAlgorithmEventHandler, IIWAlgorithmEventHandler, std::shared_ptr<DefaultIWAlgorithmEventHandler>>);
    ENTRY("IWAlgorithm", py::class_<IWAlgorithm, IAlgorithm, std::shared_ptr<IWAlgorithm>>);
    ENTRY("SIWAlgorithmStatistics", py::class_<SIWAlgorithmStatistics>);
    ENTRY("ISIWAlgorithmEventHandler", py::class_<ISIWAlgorithmEventHandler, std::shared_ptr<ISIWAlgorithmEventHandler>>);
    ENTRY("DefaultSIWAlgorithmEventHandler", py::class_<DefaultSIWAlgorithmEventHandler, ISIWAlgorithmEventHandler, std::shared_ptr<DefaultSIWAlgorithmEventHandler>>);
    ENTRY("SIWAlgorithm", py::class_<SIWAlgorithm, IAlgorithm, std::shared_ptr<SIWAlgorithm>>);
    ENTRY("StateVertex", py::class_<StateVertex>);
    ENTRY("GroundActionEdge", py::class_<GroundActionEdge>);
    ENTRY("GroundActionsEdge", py::class_<GroundActionsEdge>);
    ENTRY("StateSpaceOptions", py::class_<StateSpaceOptions>);
    ENTRY("StateSpacesOptions", py::class_<StateSpacesOptions>);
    ENTRY("StateSpace", py::class_<StateSpace, std::shared_ptr<StateSpace>>);
    ENTRY("FaithfulAbstractionOptions", py::class_<FaithfulAbstractionOptions>);
    ENTRY("FaithfulAbstractionsOptions", py::class_<FaithfulAbstractionsOptions>);
    ENTRY("FaithfulAbstractStateVertex", py::class_<FaithfulAbstractStateVertex>);
    ENTRY("FaithfulAbstraction", py::class_<FaithfulAbstraction, std::shared_ptr<FaithfulAbstraction>>);
    ENTRY("GlobalFaithfulAbstractState", py::class_<GlobalFaithfulAbstractState>);
    ENTRY("GlobalFaithfulAbstraction", py::class_<GlobalFaithfulAbstraction, std::shared_ptr<GlobalFaithfulAbstraction>>);
    ENTRY("Abstraction", py::class_<Abstraction, std::shared_ptr<Abstraction>>);
    ENTRY("EmptyVertex", py::class_<EmptyVertex>);
    ENTRY("EmptyEdge", py::class_<EmptyEdge>);
    ENTRY("StaticDigraph", py::class_<StaticDigraph>);
    ENTRY("NautyCertificate", py::class_<nauty_wrapper::Certificate, std::shared_ptr<nauty_wrapper::Certificate>>);
    ENTRY("NautyDenseGraph", py::class_<nauty_wrapper::DenseGraph>);
    ENTRY("NautySparseGraph", py::class_<nauty_wrapper::SparseGraph>);
    ENTRY("ColorFunction", py::class_<ColorFunction>);
    ENTRY("ProblemColorFunction", py::class_<ProblemColorFunction, ColorFunction>);
    ENTRY("ColoredVertex", py::class_<ColoredVertex>);
    ENTRY("StaticVertexColoredDigraph", py::class_<StaticVertexColoredDigraph>);
    ENTRY("TupleGraphVertex", py::class_<TupleGraphVertex>);
    ENTRY("TupleGraph", py::class_<TupleGraph>);
    ENTRY("TupleGraphFactory", py::class_<TupleGraphFactory>);
    ENTRY("ObjectGraphPruningStrategy", py::class_<ObjectGraphPruningStrategy>);
    ENTRY("CertificateColorRefinement", py::class_<color_refinement::Certificate>);
    ENTRY("Certificate2FWL", py::class_<kfwl::Certificate<2>>);
    ENTRY("Certificate3FWL", py::class_<kfwl::Certificate<3>>);
    ENTRY("Certificate4FWL", py::class_<kfwl::Certificate<4>>);
    ENTRY("IsomorphismTypeCompressionFunction", py::class_<kfwl::IsomorphismTypeCompressionFunction>);


    init_enums(m);
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_termobject(m);
    init_termvariable(m);
    init_termvariant(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_factories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_flatbitset(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_actions(m);

    init_domain(m);
    init_problem(m);

    class_<PDDLParser>("PDDLParser")  //
        .def(py::init<std::string, std::string>(), py::arg("domain_path"), py::arg("problem_path"))
        .def("get_domain", &PDDLParser::get_domain, py::return_value_policy::reference_internal)
        .def("get_problem", &PDDLParser::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_factories", &PDDLParser::get_pddl_factories);

    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_aag(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);

    // ObjectGraph
    m.def("create_object_graph",
          &create_object_graph,
          py::arg("color_function"),
          py::arg("pddl_factories"),
          py::arg("problem"),
          py::arg("state"),
          py::arg("state_index") = 0,
          py::arg("mark_true_goal_literals") = false,
          py::arg("pruning_strategy") = ObjectGraphPruningStrategy(),
          "Creates an object graph based on the provided parameters");
    // Color Refinement
    class_<color_refinement::Certificate>("CertificateColorRefinement")
        .def("__eq__", [](const color_refinement::Certificate& lhs, const color_refinement::Certificate& rhs) { return lhs == rhs; })
        .def("__hash__", [](const color_refinement::Certificate& self) { return std::hash<color_refinement::Certificate>()(self); })
        .def("__str__",
             [](const color_refinement::Certificate& self)
             {
                 auto os = std::stringstream();
                 os << self;
                 return os.str();
             })
        // Returning canonical compression functions does not work due to unhashable type list.
        //.def("get_canonical_configuration_compression_function", &color_refinement::Certificate::get_canonical_compression_function)
        .def("get_canonical_coloring", &color_refinement::Certificate::get_canonical_coloring);

    m.def("compute_certificate_color_refinement",
          &color_refinement::compute_certificate<StaticVertexColoredDigraph>,
          py::arg("graph"),
          "Creates color refinement certificate");

    // K-FWL
    auto bind_kfwl_certificate = [&]<size_t K>(const std::string& class_name, std::integral_constant<size_t, K>)
    {
        using CertificateK = kfwl::Certificate<K>;

        class_<CertificateK>(class_name)
            .def("__eq__", [](const CertificateK& lhs, const CertificateK& rhs) { return lhs == rhs; })
            .def("__hash__", [](const CertificateK& self) { return std::hash<CertificateK>()(self); })
            .def("__str__",
                 [](const CertificateK& self)
                 {
                     auto os = std::stringstream();
                     os << self;
                     return os.str();
                 })
            // Returning canonical compression functions does not work due to unhashable type list.
            //.def("get_canonical_isomorphic_type_compression_function", &CertificateK::get_canonical_isomorphic_type_compression_function)
            //.def("get_canonical_configuration_compression_function", &CertificateK::get_canonical_configuration_compression_function)
            .def("get_canonical_coloring", &CertificateK::get_canonical_coloring);
    };
    bind_kfwl_certificate("Certificate2FWL", std::integral_constant<size_t, 2> {});
    bind_kfwl_certificate("Certificate3FWL", std::integral_constant<size_t, 3> {});
    bind_kfwl_certificate("Certificate4FWL", std::integral_constant<size_t, 4> {});

    class_<kfwl::IsomorphismTypeCompressionFunction>("IsomorphismTypeCompressionFunction")  //
        .def(py::init<>());

    auto bind_compute_kfwl_certificate = [&]<size_t K>(const std::string& function_name, std::integral_constant<size_t, K>)
    {
        m.def(
            function_name.c_str(),
            [](const StaticVertexColoredDigraph& graph, kfwl::IsomorphismTypeCompressionFunction& iso_type_function)
            { return kfwl::compute_certificate<K>(graph, iso_type_function); },
            py::arg("static_vertex_colored_digraph"),
            py::arg("isomorphism_type_compression_function"));
    };
    bind_compute_kfwl_certificate("compute_certificate_2fwl", std::integral_constant<size_t, 2> {});
    bind_compute_kfwl_certificate("compute_certificate_3fwl", std::integral_constant<size_t, 3> {});
    bind_compute_kfwl_certificate("compute_certificate_4fwl", std::integral_constant<size_t, 4> {});
}